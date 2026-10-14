'use strict';
const fs = require('node:fs/promises'); // 文件读写与资源目录接口
const path = require('node:path'); // Windows与资源相对路径处理
const crypto = require('node:crypto'); // 文件及像素摘要校验
const { loadDependency } = require('./deps.cjs'); // 工程或捆绑环境依赖加载接口
const SIZE = 256; // 正式独立PNG的方形帧尺寸
const COLUMNS = 12; // 正式图集每行帧数
const ROWS = 5; // 正式图集行数
const FRAME_COUNT = 60; // 每个动作必须具有的实际独立帧数
const PIVOT = { x: 0.5, y: 0.92 }; // 人物统一脚底锚点

/** 加载图像编解码依赖；优先工程安装，否则使用Node运行时同级依赖目录，无参数。 */
function loadSharp() {
  return loadDependency('sharp');
}

const sharp = loadSharp(); // PNG解码、裁取、等比注册和GIF预览接口

/** 检出完整独立人物；pixels为RGBA，width/height为尺寸，expected为真实原图帧数，columns为原图列数，alphaThreshold为核心透明度。 */
function detectFigures(pixels, width, height, expected = 60, columns = COLUMNS, alphaThreshold = 128) {
  if (pixels.length !== width * height * 4) throw new Error('INVALID_RGBA');
  const labels = new Int32Array(width * height); // 像素所属的核心连通人物编号
  const queue = new Int32Array(width * height); // 八连通遍历复用队列
  const components = []; // 所有核心透明度连通分量
  let label = 0; // 当前连通分量编号
  for (let start = 0; start < labels.length; start++) { // 当前未标记核心像素位置
    if (labels[start] !== 0 || pixels[start * 4 + 3] < alphaThreshold) continue;
    label++;
    let head = 0; // 核心遍历的队列读取位置
    let tail = 1; // 核心遍历的队列写入位置
    let left = width; // 当前分量最左像素
    let right = -1; // 当前分量最右像素
    let top = height; // 当前分量最高像素
    let bottom = -1; // 当前分量最低像素
    queue[0] = start;
    labels[start] = label;
    while (head < tail) {
      const index = queue[head++]; // 当前处理的核心像素
      const x = index % width; // 核心像素横坐标
      const y = Math.floor(index / width); // 核心像素纵坐标
      left = Math.min(left, x); right = Math.max(right, x);
      top = Math.min(top, y); bottom = Math.max(bottom, y);
      for (let dy = -1; dy <= 1; dy++) { // 八邻域纵向偏移
        for (let dx = -1; dx <= 1; dx++) { // 八邻域横向偏移
          if (dx === 0 && dy === 0 || x + dx < 0 || x + dx >= width || y + dy < 0 || y + dy >= height) continue;
          const neighbor = (y + dy) * width + x + dx; // 邻域像素编号
          if (labels[neighbor] !== 0 || pixels[neighbor * 4 + 3] < alphaThreshold) continue;
          labels[neighbor] = label; queue[tail++] = neighbor;
        }
      }
    }
    components.push({ label, area: tail, left, right, top, bottom });
  }
  const maximum = Math.max(0, ...components.map(component => component.area)); // 最大人物核心面积
  const figures = components.filter(component => component.area >= Math.max(16, maximum * 0.12)); // 排除小型饰物碎片后的主体
  if (expected !== null && figures.length !== expected) throw new Error(`FRAME_COUNT: 检出${figures.length}个人物，要求${expected}；不得补复制帧`);
  if (figures.some(figure => figure.left === 0 || figure.top === 0 || figure.right === width - 1 || figure.bottom === height - 1)) throw new Error('SOURCE_CLIPPED: 原图人物触及画布边缘');
  figures.sort((first, second) => (first.top + first.bottom) - (second.top + second.bottom)); // 按人物纵向中心确定行顺序
  const heights = figures.map(figure => figure.bottom - figure.top + 1).sort((first, second) => first - second); // 人物核心高度分布
  const rowGap = heights[Math.floor(heights.length * 0.5)] * 0.4; // 同一行的人物中心与下一行之间的最小分隔
  const grouped = []; // 按实际人物行聚类的分组，不假定生成器完全遵守格子数
  for (const figure of figures) { // 当前按纵向中心排序的人物
    const center = (figure.top + figure.bottom) * 0.5; // 当前人物纵向中心
    const previous = grouped.at(-1); // 最近一行的人物分组
    if (!previous || center - previous.at(-1).center > rowGap) grouped.push([]);
    figure.center = center;
    grouped.at(-1).push(figure);
  }
  figures.length = 0;
  grouped.forEach((row, rowIndex) => { // row为实际行人物列表，rowIndex为实际源行号
    row.sort((first, second) => (first.left + first.right) - (second.left + second.right));
    for (const figure of row) { // 当前实际源行人物
      figure.sourceRow = rowIndex; figures.push(figure);
    }
  });
  const pitch = width / Math.max(1, ...grouped.map(row => row.length)); // 按最完整行估算人物实际横向间隔，row为人物行
  if (figures.some(figure => figure.right - figure.left + 1 > pitch * 1.4)) throw new Error('MERGED_FIGURES: 相邻人物相连或轮廓跨越多个位置，必须重新绘制'); // figure为疑似多个身体相连的分量
  void columns;
  return { labels, figures, components, rows: grouped.length, rowCounts: grouped.map(row => row.length) }; // row为实际源图一行
}

/** 扩张人物软边标签；pixels为源RGBA，labels为核心标签，figures为主体，width/height为源尺寸。 */
function includeSoftEdges(pixels, labels, figures, width, height) {
  const valid = new Set(figures.map(figure => figure.label)); // 允许扩张的人物核心编号
  const distances = new Uint8Array(labels.length).fill(255); // 每个软边像素与核心的八邻域距离
  const queue = new Int32Array(labels.length); // 多源扩张队列
  let head = 0; // 扩张队列读取位置
  let tail = 0; // 扩张队列写入位置
  for (let index = 0; index < labels.length; index++) { // 当前源像素编号
    if (valid.has(labels[index])) { distances[index] = 0; queue[tail++] = index; }
    else labels[index] = 0;
  }
  while (head < tail) {
    const index = queue[head++]; // 当前准备扩张的像素编号
    if (distances[index] >= 10) continue;
    const x = index % width; // 当前扩张像素横坐标
    const y = Math.floor(index / width); // 当前扩张像素纵坐标
    for (let dy = -1; dy <= 1; dy++) { // 邻域纵向偏移
      for (let dx = -1; dx <= 1; dx++) { // 邻域横向偏移
        if (dx === 0 && dy === 0 || x + dx < 0 || x + dx >= width || y + dy < 0 || y + dy >= height) continue;
        const neighbor = (y + dy) * width + x + dx; // 当前候选软边像素
        if (distances[neighbor] !== 255 || pixels[neighbor * 4 + 3] === 0) continue;
        distances[neighbor] = distances[index] + 1;
        labels[neighbor] = labels[index]; queue[tail++] = neighbor;
      }
    }
  }
  for (let index = 0; index < labels.length; index++) { // 当前已经归属人物的软边像素
    if (labels[index] === 0 || pixels[index * 4 + 3] < 32) continue;
    const x = index % width; // 当前有效轮廓横坐标
    const y = Math.floor(index / width); // 当前有效轮廓纵坐标
    if (x === 0 || y === 0 || x === width - 1 || y === height - 1) throw new Error('SOURCE_CLIPPED: 人物半透明轮廓触及源画布边缘');
  }
}

/** 检查绝对路径范围；root为允许操作的素材根目录，target为即将移动的文件或目录。 */
function assertInside(root, target) {
  const relative = path.relative(path.resolve(root), path.resolve(target)); // 已解析目标相对允许根目录的路径
  if (relative === '..' || relative.startsWith(`..${path.sep}`) || path.isAbsolute(relative)) throw new Error('PATH_OUTSIDE_ASSET_ROOT');
}

/** 提交完整暂存资源；root为素材根目录，stage为组暂存目录，targets为明确相对路径，不删除历史资源。 */
async function commitStaged(root, stage, targets) {
  root = path.resolve(root); stage = path.resolve(stage);
  assertInside(root, stage);
  const backup = path.join(root, '.history', path.basename(stage)); // 用于保留旧资源和异常回滚的目录
  assertInside(root, backup);
  for (const relative of targets) { // 当前必须完整存在的暂存输出相对路径
    const source = path.resolve(stage, relative); // 已解析暂存资源绝对路径
    const destination = path.resolve(root, relative); // 已解析正式资源绝对路径
    assertInside(stage, source); assertInside(root, destination);
    await fs.stat(source);
  }
  const operations = []; // 已成功执行的移动记录，用于出错时逆序还原
  try {
    for (const relative of targets) { // 当前完整资源的相对路径
      const source = path.resolve(stage, relative); // 当前暂存资源绝对位置
      const destination = path.resolve(root, relative); // 当前正式资源绝对位置
      const previous = path.resolve(backup, relative); // 当前旧资源保存位置
      assertInside(stage, source); assertInside(root, destination); assertInside(backup, previous);
      await fs.mkdir(path.dirname(destination), { recursive: true });
      let exists = false; // 正式资源是否已有旧版本
      try { await fs.stat(destination); exists = true; }
      catch (error) { if (error.code !== 'ENOENT') throw error; } // error为正式资源查询错误
      if (exists) {
        await fs.mkdir(path.dirname(previous), { recursive: true });
        await fs.rename(destination, previous);
        operations.push({ source: destination, destination: previous });
      }
      await fs.rename(source, destination);
      operations.push({ source, destination });
    }
  } catch (error) { // 暂存提交失败，恢复已移动的旧版本
    for (const operation of operations.reverse()) { // 当前需要逆向恢复的已完成移动
      assertInside(root, operation.source); assertInside(root, operation.destination);
      await fs.rename(operation.destination, operation.source);
    }
    throw error;
  }
}

/** 计算像素摘要；data为需要验证的实际文件或RGBA像素。 */
function hash(data) { return crypto.createHash('sha256').update(data).digest('hex'); }

/** 计算60帧GIF延迟；duration为完整动作秒数，按10毫秒量化并保持总时长。 */
function previewDelays(duration) {
  return Array.from({ length: FRAME_COUNT }, (_, index) => Math.max(10, (Math.round((index + 1) * duration * 100 / FRAME_COUNT) - Math.round(index * duration * 100 / FRAME_COUNT)) * 10)); // index为动作帧号
}

/** 无损拼接图集；rawFrames为60张256方形RGBA，直接复制字节防止半透明合成舍入。 */
async function encodeAtlas(rawFrames) {
  if (rawFrames.length !== FRAME_COUNT || rawFrames.some(frame => frame.length !== SIZE * SIZE * 4)) throw new Error('INVALID_ATLAS_FRAMES'); // frame为输入像素帧
  const width = SIZE * COLUMNS; // 正式图集像素宽度
  const height = SIZE * ROWS; // 正式图集像素高度
  const canvas = Buffer.alloc(width * height * 4); // 保留原始RGBA的图集画布
  rawFrames.forEach((frame, index) => { // frame为独立帧像素，index为按行排列的序号
    const left = index % COLUMNS * SIZE; // 当前帧图集横坐标
    const top = Math.floor(index / COLUMNS) * SIZE; // 当前帧图集纵坐标
    for (let y = 0; y < SIZE; y++) frame.copy(canvas, ((top + y) * width + left) * 4, y * SIZE * 4, (y + 1) * SIZE * 4); // y为当前帧像素行
  });
  const png = await sharp(canvas, { raw: { width, height, channels: 4 } }).png().toBuffer(); // 直接编码而不再进行透明度混合
  const decoded = await sharp(png).raw().toBuffer(); // 编码后实际解码像素
  if (!decoded.equals(canvas)) throw new Error('ATLAS_PIXEL_MISMATCH');
  return png;
}

/** 比较相邻帧差异；first/second为同尺寸RGBA，使用预乘颜色避免透明底色干扰。 */
function frameDifference(first, second) {
  let sum = 0; // 所有像素的预乘颜色和透明度绝对差总和
  for (let index = 0; index < first.length; index += 4) { // 当前RGBA像素偏移
    const alphaA = first[index + 3] / 255; // 首帧当前像素的归一化透明度
    const alphaB = second[index + 3] / 255; // 次帧当前像素的归一化透明度
    sum += Math.abs(alphaA - alphaB);
    for (let channel = 0; channel < 3; channel++) { // RGB颜色通道编号
      sum += Math.abs(first[index + channel] * alphaA - second[index + channel] * alphaB) / 255;
    }
  }
  return sum / first.length;
}

/** 估计躯干根部横坐标；pixels/labels为图像，figure为人物，width为图宽，以深色躯干抑制头发与衣摆的拖尾偏差。 */
function bodyCenter(pixels, labels, figure, width) {
  const positions = []; // 核心躯干深色像素的横坐标列表
  const height = figure.bottom - figure.top + 1; // 人物核心高度
  for (let y = Math.floor(figure.top + height * 0.35); y <= Math.floor(figure.top + height * 0.66); y++) { // 躯干中段的源图行
    for (let x = figure.left; x <= figure.right; x++) { // 躯干中段的源图列
      const offset = (y * width + x) * 4; // 当前像素RGBA起点
      if (labels[y * width + x] !== figure.label) continue;
      if (pixels[offset] < 150 && pixels[offset + 1] < 120 && pixels[offset + 2] < 120 && Math.abs(pixels[offset] - pixels[offset + 1]) < 65) positions.push(x);
    }
  }
  if (positions.length < 8) return (figure.left + figure.right) * 0.5;
  positions.sort((first, second) => first - second); // 深色躯干像素由左至右排序
  return positions[Math.floor(positions.length * 0.5)];
}

/** 注册单帧；pixels/labels为源图，figure为人物，index为帧序，width/height为源尺寸，baseline为脚底基线，heightRatio为站姿尺度修正。 */
async function registerFrame(pixels, labels, figure, index, width, height, baseline, heightRatio, sourceRows = ROWS) {
  let left = width; // 包含柔和发梢和衣摆后的最左像素
  let right = -1; // 人物软边最右像素
  let top = height; // 人物软边最高像素
  let bottom = -1; // 人物软边最低像素
  for (let pixel = 0; pixel < labels.length; pixel++) { // 当前源图像素序号
    if (labels[pixel] !== figure.label) continue;
    const x = pixel % width; // 当前人物像素横坐标
    const y = Math.floor(pixel / width); // 当前人物像素纵坐标
    left = Math.min(left, x); right = Math.max(right, x);
    top = Math.min(top, y); bottom = Math.max(bottom, y);
  }
  const cropWidth = right - left + 1; // 包含完整人物软边的裁取宽度
  const cropHeight = bottom - top + 1; // 包含完整人物软边的裁取高度
  const crop = Buffer.alloc(cropWidth * cropHeight * 4); // 仅保留本人物像素的透明独立帧
  for (let y = top; y <= bottom; y++) { // 人物裁取区域源图行
    for (let x = left; x <= right; x++) { // 人物裁取区域源图列
      const source = y * width + x; // 源图中的像素编号
      if (labels[source] !== figure.label) continue;
      pixels.copy(crop, ((y - top) * cropWidth + x - left) * 4, source * 4, source * 4 + 4);
    }
  }
  const cellHeight = height / sourceRows; // 按实测行数计算的源格高度
  const scale = SIZE * 0.86 / cellHeight * heightRatio; // 等比缩放，稳定站姿动作的绘制尺度，非等比变形或补帧
  const targetWidth = Math.max(1, Math.round(cropWidth * scale)); // 等比缩放后的完整人物宽度
  const targetHeight = Math.max(1, Math.round(cropHeight * scale)); // 等比缩放后的完整人物高度
  const targetLeft = Math.round(SIZE * PIVOT.x + (left - bodyCenter(pixels, labels, figure, width)) * scale); // 躯干根部对齐统一横坐标，去除整个人物的绘制漂移
  const targetTop = Math.round(SIZE * PIVOT.y + (top - figure.sourceRow * cellHeight - baseline) * scale); // 使用真实源行及脚底定位，支持从更多新绘帧中选择60帧
  if (targetLeft < 1 || targetTop < 1 || targetLeft + targetWidth >= SIZE || targetTop + targetHeight >= SIZE) throw new Error(`REGISTER_CLIPPED: ${index}完整人物超出正式帧边界 ${JSON.stringify({ left, right, top, bottom, targetLeft, targetTop, targetWidth, targetHeight, core: figure })}`);
  const resized = await sharp(crop, { raw: { width: cropWidth, height: cropHeight, channels: 4 } }).resize(targetWidth, targetHeight).png().toBuffer(); // 完整人物等比注册结果
  return sharp({ create: { width: SIZE, height: SIZE, channels: 4, background: '#00000000' } }).composite([{ input: resized, left: targetLeft, top: targetTop }]).png().toBuffer();
}

/** 导出一组真实重绘素材；root为资源根目录，clip为动作计划，source为实际生成原图路径。 */
async function packClip(root, clip, source) {
  let layout = { sources: [{ image: path.basename(source), expected: FRAME_COUNT }] }; // 默认一张原图包含60帧，不自动填重复帧
  try { layout = JSON.parse(await fs.readFile(path.join(root, 'raw', `${clip.id}-source-layout.json`), 'utf8')); }
  catch (error) { // 可选真实源图帧选择清单的读取错误
    if (error.code !== 'ENOENT') throw error;
  }
  const records = []; // 正式时间线中选中的真实新绘人物与来源
  const sources = []; // 已解码的实际新绘源图及验证记录
  for (const entry of layout.sources) { // 当前真实新绘图及选择的连续或均匀采样帧
    const sourcePath = path.join(root, 'raw', entry.image); // 当前新绘图绝对路径
    const decoded = await sharp(sourcePath).ensureAlpha().raw().toBuffer({ resolveWithObject: true }); // 实际源图RGBA与尺寸
    const detected = detectFigures(decoded.data, decoded.info.width, decoded.info.height, entry.expected, COLUMNS, entry.alphaThreshold ?? 128); // 真实来源帧数与排序检查
    includeSoftEdges(decoded.data, detected.labels, detected.figures, decoded.info.width, decoded.info.height);
    const cellHeight = decoded.info.height / detected.rows; // 当前源图实际每行高度
    const selected = entry.indices ?? Array.from({ length: detected.figures.length }, (_, index) => index); // 当前明确选择的原图人物序号
    if (new Set(selected).size !== selected.length || selected.some(index => !Number.isInteger(index) || index < 0 || index >= detected.figures.length)) throw new Error('INVALID_SOURCE_INDICES'); // index为真实来源帧号
    const sourceRecord = { image: `raw/${entry.image}`, sha256: hash(await fs.readFile(sourcePath)), nativeWidth: decoded.info.width, nativeHeight: decoded.info.height, actualFigures: detected.figures.length, selectedIndices: selected, rows: detected.rows, rowCounts: detected.rowCounts, method: 'image_gen fresh redraw; no interpolation or repeated-frame padding' }; // 保留真实新绘来源及选帧依据
    sources.push(sourceRecord);
    for (const sourceIndex of selected) { // 当前选择的真实原图人物序号
      records.push({ data: decoded.data, info: decoded.info, labels: detected.labels, figure: detected.figures[sourceIndex], sourceIndex, sourceRows: detected.rows, source: sourceRecord.image });
    }
  }
  if (records.length !== FRAME_COUNT) throw new Error(`FRAME_COUNT: 实际选择${records.length}帧，要求60；禁止重复补齐`);
  if (!/^[a-zA-Z0-9_-]+$/.test(clip.id)) throw new Error('INVALID_CLIP_ID');
  const outputRoot = path.join(root, '.staging', `${clip.id}-${crypto.randomBytes(6).toString('hex')}`); // 整组验证之前只写暂存目录
  assertInside(root, outputRoot);
  const directory = path.join(outputRoot, 'frames', clip.id); // 当前动作暂存的六十张独立PNG目录
  await fs.mkdir(directory, { recursive: true });
  await fs.mkdir(path.join(outputRoot, 'atlases'), { recursive: true });
  await fs.mkdir(path.join(outputRoot, 'previews'), { recursive: true });
  const buffers = []; // 六十张正式帧PNG字节
  const rawFrames = []; // 六十张正式帧RGBA，用于像素一致性和首尾差异检查
  const frames = []; // 正式帧清单及图集矩形
  const pixelHashes = new Set(); // 不重复的真实像素摘要
  const normalizedHeights = records.map(record => (record.figure.bottom - record.figure.top + 1) / (record.info.height / record.sourceRows)).sort((first, second) => first - second); // 跨源图可比较的人物相对高度
  const medianHeight = normalizedHeights[Math.floor(normalizedHeights.length * 0.5)]; // 站姿动作的共同人物相对高度
  const stabilizeHeight = ['slow_walk', 'idle', 'turn'].includes(clip.action); // 不改变姿态高度的动作只做等比尺度稳定
  const isLoop = clip.loop ?? !['turn', 'crouch_to_stand', 'stand_to_jump', 'stand_to_dash', 'crouch_to_dash_to_stand'].includes(clip.action); // 当前动作是否循环
  let previous = null; // 前一段末帧的真实姿态，用于新补绘段的等比尺度衔接
  let currentSource = ''; // 最近处理的真实源图标识
  let sourceScale = 1; // 当前源图整段共用的等比注册系数，不改变任何绘制姿势
  for (const record of records) { // 当前按正式时间线排列的实绘帧
    if (record.source !== currentSource) {
      if (previous && !stabilizeHeight) {
        const endpoint = isLoop ? records[0] : previous; // 循环末段对齐循环首姿，非循环对齐前段交接姿
        const nextAnchor = isLoop ? records.filter(item => item.source === record.source).at(-1) : record; // item为同来源的实际新绘帧；循环使用最后姿态，非循环使用首姿
        const targetHeight = (endpoint.figure.bottom - endpoint.figure.top + 1) / (endpoint.info.height / endpoint.sourceRows) * endpoint.sourceScale; // 交接目标的已注册相对轮廓高度
        const anchorHeight = (nextAnchor.figure.bottom - nextAnchor.figure.top + 1) / (nextAnchor.info.height / nextAnchor.sourceRows); // 新源图交接锚点的相对轮廓高度
        sourceScale = targetHeight / anchorHeight;
      }
      currentSource = record.source;
    }
    record.sourceScale = sourceScale; previous = record;
  }
  let fitRatio = 1; // 整组共用的安全等比系数，避免大姿态逐帧缩放造成尺寸跳动
  for (const record of records) { // 当前真实新绘帧的完整轮廓估计
    const cellHeight = record.info.height / record.sourceRows; // 当前来源每行高度
    const baseline = record.figure.bottom - record.figure.sourceRow * cellHeight; // 每张PNG保持同一脚底锚点；跳跃空间位移由游戏物理处理
    const heightRatio = stabilizeHeight ? medianHeight / ((record.figure.bottom - record.figure.top + 1) / cellHeight) : record.sourceScale; // 当前站姿或源图交接的等比稳定系数
    const scale = SIZE * 0.86 / cellHeight * heightRatio; // 未加安全系数的基础等比倍率
    const center = bodyCenter(record.data, record.labels, record.figure, record.info.width); // 当前躯干横向根部
    const extents = [(center - record.figure.left + 10) * scale, (record.figure.right - center + 10) * scale, (record.figure.sourceRow * cellHeight + baseline - record.figure.top + 10) * scale, (record.figure.bottom + 10 - record.figure.sourceRow * cellHeight - baseline) * scale]; // 将10像素软边扩张计入左右上下安全范围
    const limits = [120, 120, 227, 13]; // 对应统一脚底与画布边缘之间可用的距离
    extents.forEach((extent, index) => { if (extent > 0) fitRatio = Math.min(fitRatio, limits[index] / extent); }); // extent为轮廓距离，index为安全方向
  }
  for (let index = 0; index < FRAME_COUNT; index++) { // 当前正式帧序号
    const record = records[index]; // 当前真实新绘人物与所在源图
    const cellHeight = record.info.height / record.sourceRows; // 当前真实源图的每行高度
    const baseline = record.figure.bottom - record.figure.sourceRow * cellHeight; // 统一脚底根部，避免来源图的地面估计导致补绘段突然掉落
    const heightRatio = (stabilizeHeight ? medianHeight / ((record.figure.bottom - record.figure.top + 1) / cellHeight) : record.sourceScale) * fitRatio; // 整组等比适配画布及交接源图尺度，保留蹲跳姿态高度
    const png = await registerFrame(record.data, record.labels, record.figure, index, record.info.width, record.info.height, baseline, heightRatio, record.sourceRows); // 当前完整人物的注册PNG
    const image = `frames/${clip.id}/${String(index).padStart(2, '0')}.png`; // 当前帧的资源相对路径
    const raw = await sharp(png).ensureAlpha().raw().toBuffer(); // 当前正式PNG的真实RGBA像素
    const pixelHash = hash(raw); // 当前正式帧的像素摘要
    pixelHashes.add(pixelHash); buffers.push(png); rawFrames.push(raw);
    frames.push({ index, image, sourceImage: record.source, sourceFrame: record.sourceIndex, sha256: hash(png), pixelSha256: pixelHash, rect: { x: index % COLUMNS * SIZE, y: Math.floor(index / COLUMNS) * SIZE, width: SIZE, height: SIZE } });
  }
  if (pixelHashes.size !== FRAME_COUNT) throw new Error(`DUPLICATED_FRAMES: ${clip.id}仅有${pixelHashes.size}种实际像素帧`);
  for (let index = 0; index < FRAME_COUNT; index++) { // 通过完整注册和不重复校验后写入的正式帧序号
    await fs.writeFile(path.join(outputRoot, frames[index].image), buffers[index]);
  }
  const atlasImage = `atlases/${clip.id}.png`; // 该动作的正式图集路径
  await fs.writeFile(path.join(outputRoot, atlasImage), await encodeAtlas(rawFrames));
  const loop = clip.loop ?? !['turn', 'crouch_to_stand', 'stand_to_jump', 'stand_to_dash', 'crouch_to_dash_to_stand'].includes(clip.action); // 动作是否循环
  const duration = clip.action === 'idle' ? 2.0 : clip.action === 'turn' ? 0.75 : 1.0; // 预览默认整段持续秒数，可由游戏重定时
  const delays = previewDelays(duration); // GIF以10毫秒量化的真实帧延迟
  const previewImage = `previews/${clip.id}.gif`; // 当前动作的观看用GIF
  await sharp(buffers, { join: { animated: true } }).gif({ delay: delays, loop: loop ? 0 : 1, keepDuplicateFrames: true, effort: 5 }).toFile(path.join(outputRoot, previewImage));
  const gifInfo = await sharp(path.join(outputRoot, previewImage), { animated: true }).metadata(); // 暂存GIF真实解码得到的页数
  if (gifInfo.pages !== FRAME_COUNT || gifInfo.width !== SIZE || gifInfo.pageHeight !== SIZE) throw new Error('INVALID_PREVIEW');
  const differences = rawFrames.slice(1).map((raw, index) => frameDifference(rawFrames[index], raw)); // 相邻图像差异，raw为当前帧，index为上一帧序号
  const sortedDifferences = [...differences].sort((first, second) => first - second); // 从小到大排列的正常帧差异
  const seamDifference = frameDifference(rawFrames[59], rawFrames[0]); // 第59到第00帧的实际预乘像素差异
  const seamLimit = sortedDifferences[Math.floor(sortedDifferences.length * 0.95)] * 1.4 + 0.01; // 异常循环断点的检测阈值
  if (loop) {
    const seamIndices = [56, 57, 58, 59, 0, 1, 2, 3]; // 循环接缝的独立检查帧序列
    await sharp(seamIndices.map(index => buffers[index]), { join: { animated: true } }).gif({ delay: 150, loop: 0, effort: 5 }).toFile(path.join(outputRoot, 'previews', `${clip.id}-seam.gif`)); // index为循环接缝帧号
  }
  const result = { ...clip, prompt: undefined, parts: undefined, frameCount: FRAME_COUNT, frameSize: SIZE, pixelFormat: 'RGBA', pivot: PIVOT, atlas: { image: atlasImage, columns: COLUMNS, rows: ROWS, width: SIZE * COLUMNS, height: SIZE * ROWS }, preview: { image: previewImage, delayMs: delays }, loop, durationSeconds: duration, suggestedFps: FRAME_COUNT / duration, frames, sources, verification: { sourceFigures: sources.reduce((sum, item) => sum + item.actualFigures, 0), distinctPixelFrames: pixelHashes.size, seamDifference, seamLimit, seamMetricPassed: !loop || seamDifference <= seamLimit, visualContinuityVerified: false } }; // sum为来源总帧数，item为来源记录；数值检查不等同于人工动作验收
  await fs.writeFile(path.join(outputRoot, 'atlases', `${clip.id}.json`), JSON.stringify({ frames: Object.fromEntries(frames.map(frame => [`${clip.id}_${String(frame.index).padStart(2, '0')}`, { frame: { x: frame.rect.x, y: frame.rect.y, w: SIZE, h: SIZE }, rotated: false, trimmed: false, sourceSize: { w: SIZE, h: SIZE }, spriteSourceSize: { x: 0, y: 0, w: SIZE, h: SIZE }, pivot: PIVOT }])), meta: { image: `${clip.id}.png`, size: { w: SIZE * COLUMNS, h: SIZE * ROWS }, scale: '1', format: 'RGBA8888' } }, null, 2)); // frame为正式帧清单
  await fs.mkdir(path.join(outputRoot, 'metadata'), { recursive: true });
  await fs.writeFile(path.join(outputRoot, 'metadata', `${clip.id}.json`), JSON.stringify(result, null, 2));
  const targets = [`frames/${clip.id}`, atlasImage, `atlases/${clip.id}.json`, previewImage, `metadata/${clip.id}.json`]; // 只有整组通过后才替换的明确资源路径
  if (loop) targets.push(`previews/${clip.id}-seam.gif`);
  await commitStaged(root, outputRoot, targets);
  return result;
}

/** 命令行入口；argv为inspect/pack参数及源图、资源目录、动作计划路径。 */
async function main(argv) {
  if (argv[0] === 'anchors') {
    const decoded = await sharp(argv[1]).ensureAlpha().raw().toBuffer({ resolveWithObject: true }); // 需要提取交接参考帧的实际新绘原图
    const detected = detectFigures(decoded.data, decoded.info.width, decoded.info.height, null); // 读取真实人物数量，不填补缺帧
    includeSoftEdges(decoded.data, detected.labels, detected.figures, decoded.info.width, decoded.info.height);
    const heights = detected.figures.map(figure => figure.bottom - figure.top + 1).sort((first, second) => first - second); // 参考图站姿人物高度分布
    const natural = argv[4] === 'natural'; // 非站姿参考保留真实姿态高度，不按身体轮廓放大蹲跳冲刺
    const indices = argv[3].split(',').map(Number); // 明确需要提取的原图人物帧号
    await fs.mkdir(argv[2], { recursive: true });
    for (const index of indices) { // 当前需要提取的真实交接帧号
      const figure = detected.figures[index]; // 当前真实新绘参考人物
      if (!figure) throw new Error('INVALID_ANCHOR_INDEX');
      const cellHeight = decoded.info.height / detected.rows; // 当前新绘原图每行高度
      const baseline = figure.bottom - figure.sourceRow * cellHeight; // 当前站立参考帧实测脚底
      const heightRatio = Math.min(natural ? 1 : heights[Math.floor(heights.length * 0.5)] / (figure.bottom - figure.top + 1), 218 * cellHeight / (SIZE * 0.86 * (figure.bottom - figure.top + 21)), 200 * cellHeight / (SIZE * 0.86 * (figure.right - figure.left + 21))); // 参考图安全等比尺度，保留蹲姿且不截断高大的站姿
      const png = await registerFrame(decoded.data, detected.labels, figure, index, decoded.info.width, decoded.info.height, baseline, heightRatio, detected.rows); // 完整等比注册的交接参考帧
      await fs.writeFile(path.join(argv[2], `anchor-${index}.png`), png);
    }
    return;
  }
  if (argv[0] === 'inspect' || argv[0] === 'inspect-flex') {
    const decoded = await sharp(argv[1]).ensureAlpha().raw().toBuffer({ resolveWithObject: true }); // 待检查的实际生成PNG
    const result = detectFigures(decoded.data, decoded.info.width, decoded.info.height, argv[0] === 'inspect-flex' ? null : FRAME_COUNT); // 严格60帧或只读真实人物计数
    console.log(JSON.stringify({ count: result.figures.length, rows: result.rows, rowCounts: result.rowCounts, width: decoded.info.width, height: decoded.info.height }));
    return;
  }
  if (argv[0] !== 'pack' || argv.length !== 4) throw new Error('用法：pack.cjs pack <资源根目录> <完整计划JSON> <动作id>');
  const root = path.resolve(argv[1]); // 正式素材输出根目录
  const plan = JSON.parse(await fs.readFile(argv[2], 'utf8')); // 本轮用户确认的全部动作范围
  const clip = plan.clips.find(item => item.id === argv[3]); // 当前需要打包的动作定义
  if (!clip) throw new Error('UNKNOWN_CLIP');
  const result = await packClip(root, clip, path.join(root, 'raw', `${clip.id}-full.png`)); // 当前动作真实导出和验证结果
  console.log(JSON.stringify({ id: clip.id, frameCount: result.frameCount, verification: result.verification }));
}

module.exports = { detectFigures, includeSoftEdges, packClip, frameDifference, commitStaged, previewDelays, encodeAtlas };
if (require.main === module) main(process.argv.slice(2)).catch(error => { console.error(error.message); process.exitCode = 1; }); // error为导出或验证失败原因
