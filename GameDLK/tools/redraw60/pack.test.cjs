'use strict';
const test = require('node:test'); // Node内置行为测试接口
const assert = require('node:assert/strict'); // 严格断言接口
const fs = require('node:fs/promises'); // 回滚行为测试的文件接口
const path = require('node:path'); // 测试目录路径接口
const os = require('node:os'); // 当前系统临时目录接口
const { detectFigures, includeSoftEdges, previewDelays, commitStaged, encodeAtlas, sourceScaleFor, packClip } = require('./pack.cjs'); // 主体检测、播放时长、事务提交、无损图集、分段尺度和真实打包函数
const sharp = require('./deps.cjs').loadDependency('sharp'); // 验证实际PNG解码像素

/** 构造尺度交接姿态；height为轮廓高度，cell为源图行高，source为来源，mode为交接方式。 */
function scalePose(height, cell, source, mode) {
  return { figure: { top: 0, bottom: height - 1 }, info: { height: cell }, sourceRows: 1, source, scaleMode: mode, sourceScale: 1 };
}

test('多段跳跃的中段按上一段交接姿态注册，不能放大到循环待机高度', () => {
  const first = scalePose(180, 200, 'initial'); // 循环首帧的站立姿态
  const previous = scalePose(90, 200, 'initial'); // 蓄力末帧的蹲姿
  const next = scalePose(100, 400, 'airborne', 'previous'); // 下一段首帧蹲姿，原图行高不同
  const end = scalePose(120, 400, 'airborne', 'previous'); // 下一段末帧已腾空，不能用于站姿尺度
  assert.equal(sourceScaleFor([first, previous, next, end], next, previous, true), 1.8);
});

test('指定循环闭合段仍按末姿与初始待机匹配等比尺度', () => {
  const first = scalePose(180, 200, 'initial'); // 循环首帧站立
  const previous = scalePose(90, 200, 'initial'); // 上一段落地蹲姿
  const next = scalePose(100, 400, 'recovery', 'loop-start'); // 起身段首姿
  const end = scalePose(180, 400, 'recovery', 'loop-start'); // 起身段末姿
  assert.equal(sourceScaleFor([first, previous, next, end], next, previous, true), 2);
});

test('交接尺度模式写错时拒绝导出，不能静默改变整段人物大小', () => {
  const previous = scalePose(180, 200, 'initial'); // 合法前段末姿
  const next = scalePose(100, 200, 'next', 'typo'); // 错误的源图交接设置
  assert.throws(() => sourceScaleFor([previous, next], next, previous, true), /INVALID_SCALE_MODE/);
});

test('同规格分段不能因为屈膝和发梢高度不同而累计缩小人体', () => {
  const previous = scalePose(250, 512, 'takeoff'); // 前段轮廓包含后扬头发与弯曲双腿
  previous.sourceScale = 0.7;
  const next = scalePose(360, 512, 'apex', 'native'); // 下一段人体同尺寸，但跳跃姿态改变轮廓高度
  assert.equal(sourceScaleFor([previous, next], next, previous, true), 1);
});

/** 构造可数的透明测试图；count为人物数，edge决定首个人物是否被画布边缘裁切。 */
function fixture(count = 60, edge = false) {
  const width = 120; // 测试图的宽度
  const height = 80; // 测试图的高度
  const pixels = Buffer.alloc(width * height * 4); // 六十个完全分离的透明测试帧
  for (let index = 0; index < count; index++) { // 当前测试人物的行优先序号
    const left = index % 12 * 10 + (index === 0 && edge ? 0 : 2); // 当前人物左边缘
    const top = Math.floor(index / 12) * 16 + 3; // 当前人物上边缘
    for (let y = top; y < top + 10; y++) { // 当前人物占用像素行
      for (let x = left; x < left + 6; x++) { // 当前人物占用像素列
        const offset = (y * width + x) * 4; // RGBA像素起始位置
        pixels[offset] = index + 1;
        pixels[offset + 1] = 100;
        pixels[offset + 2] = 150;
        pixels[offset + 3] = 255;
      }
    }
  }
  return { pixels, width, height };
}

test('内存来源清单失败不改旧清单，成功后与正式资源一起提交', async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), 'redraw-layout-')); // 本测试独立临时根目录
  const rawDirectory = path.join(directory, 'raw'); // 测试原图及已交付清单目录
  const layoutPath = path.join(rawDirectory, 'phase-source-layout.json'); // 已交付的真实来源清单
  const oldLayout = '{"sources":[{"image":"old-missing.png","expected":60}]}'; // 旧资源依赖，不能被失败发布覆盖
  const inputPath = path.join(rawDirectory, 'input.png'); // 用实际PNG验证内存清单优先级
  const layout = { sources: [{ image: 'input.png', expected: 60 }], frameOrder: Array.from({ length: 60 }, (_, index) => 59 - index) }; // 新清单明确指定60张实绘图的播放顺序，index为原时间线位置
  try {
    await fs.mkdir(rawDirectory);
    await fs.writeFile(layoutPath, oldLayout);
    const missing = fixture(59); // 实际只有59张，必须在发布前拒绝
    await sharp(missing.pixels, { raw: { width: missing.width, height: missing.height, channels: 4 } }).png().toFile(inputPath);
    await assert.rejects(packClip(directory, { id: 'phase', action: 'jump', loop: false }, inputPath, layout), /FRAME_COUNT/);
    assert.equal(await fs.readFile(layoutPath, 'utf8'), oldLayout);
    const complete = fixture(60); // 60张有不同颜色的真实输入，能够通过实际打包
    await sharp(complete.pixels, { raw: { width: complete.width, height: complete.height, channels: 4 } }).png().toFile(inputPath);
    const result = await packClip(directory, { id: 'phase', action: 'jump', loop: false }, inputPath, layout); // 实际导出的来源与播放序列
    assert.equal(result.frames[0].sourceFrame, 59);
    assert.equal(result.frames[59].sourceFrame, 0);
    assert.deepEqual(JSON.parse(await fs.readFile(layoutPath, 'utf8')), layout);
    assert.equal((await fs.readdir(path.join(directory, 'frames', 'phase'))).length, 60);
    await assert.rejects(packClip(directory, { id: 'phase', action: 'jump', loop: false }, inputPath, { ...layout, frameOrder: Array(60).fill(0) }), /INVALID_FRAME_ORDER/);
    assert.deepEqual(JSON.parse(await fs.readFile(layoutPath, 'utf8')), layout);
  } finally {
    if (!path.resolve(directory).startsWith(path.resolve(os.tmpdir()) + path.sep + 'redraw-layout-')) throw new Error('UNSAFE_TEST_DIRECTORY');
    await fs.rm(directory, { recursive: true, force: true });
  }
});

test('六十个完整人物必须精确检出并按行排序', () => {
  const input = fixture(); // 包含六十个独立人物的真实RGBA测试数据
  const result = detectFigures(input.pixels, input.width, input.height); // 生产检测函数的实际结果
  assert.equal(result.figures.length, 60);
  assert.equal(result.figures[0].left, 2);
  assert.equal(result.figures[11].left, 112);
  assert.equal(result.figures[12].top, 19);
});

test('五十九帧不得用复制或重复编号伪装成六十帧', () => {
  const input = fixture(59); // 少一帧的输入图
  assert.throws(() => detectFigures(input.pixels, input.width, input.height), /FRAME_COUNT/);
});

test('两个人物轮廓相连时不得误当一张可用帧并缩小整组', () => {
  const input = fixture(); // 正常60人测试图
  for (let x = 8; x <= 11; x++) input.pixels[(8 * input.width + x) * 4 + 3] = 255; // x为连接前两个人物的错误轮廓
  assert.throws(() => detectFigures(input.pixels, input.width, input.height, null), /MERGED_FIGURES/);
});

test('人物触及画布边缘时必须报告源图裁切风险', () => {
  const input = fixture(60, true); // 第一帧触及画布左边缘的输入图
  assert.throws(() => detectFigures(input.pixels, input.width, input.height), /SOURCE_CLIPPED/);
});

test('半透明发梢触边也必须拒绝，即使不透明主体没有触边', () => {
  const input = fixture(); // 不透明主体均在安全范围内的输入图
  for (const x of [0, 1]) { // 连接首个人物到画布边缘的半透明轮廓位置
    const offset = (8 * input.width + x) * 4; // 半透明轮廓像素偏移
    input.pixels[offset] = 150;
    input.pixels[offset + 3] = 127;
  }
  const detected = detectFigures(input.pixels, input.width, input.height); // 主体本身仍满足60帧和核心裁切检查
  assert.throws(() => includeSoftEdges(input.pixels, detected.labels, detected.figures, input.width, input.height), /SOURCE_CLIPPED/);
});

test('60帧预览总时长应为指定的一秒而非六十秒', () => {
  const delays = previewDelays(1); // 60帧的一秒播放延迟
  assert.equal(delays.length, 60);
  assert.equal(delays.reduce((sum, delay) => sum + delay, 0), 1000); // sum为累计毫秒，delay为当前帧延迟
  assert.ok(delays.every(delay => delay >= 10 && delay % 10 === 0)); // delay为GIF的10毫秒量化延迟
});

test('缺少组内暂存文件时不得覆盖已交付的旧帧', async () => {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), 'mx002-pack-')); // 本测试专属安全目录
  const stage = path.join(root, '.staging', 'test'); // 模拟尚未完整写出的暂存组
  await fs.mkdir(path.join(stage, 'frames'), { recursive: true });
  await fs.mkdir(path.join(root, 'frames'), { recursive: true });
  await fs.writeFile(path.join(root, 'frames', '00.png'), 'old-frame');
  await fs.writeFile(path.join(stage, 'frames', '00.png'), 'new-frame');
  await assert.rejects(commitStaged(root, stage, ['frames/00.png', 'missing-atlas.png']), /ENOENT/);
  assert.equal(await fs.readFile(path.join(root, 'frames', '00.png'), 'utf8'), 'old-frame');
});

test('图集编码不得改写半透明RGB，特别是alpha171的红色发梢', async () => {
  const frames = Array.from({ length: 60 }, (_, index) => Buffer.alloc(256 * 256 * 4)); // 60张透明测试帧，index为帧号
  frames[0].set([207, 22, 22, 171], (76 * 256 + 128) * 4);
  frames[59].set([131, 37, 29, 127], (180 * 256 + 60) * 4);
  const png = await encodeAtlas(frames); // 使用实际生产路径输出PNG
  const decoded = await sharp(png).raw().toBuffer({ resolveWithObject: true }); // 实际PNG解码信息及字节
  assert.deepEqual([...decoded.data.subarray((76 * 3072 + 128) * 4, (76 * 3072 + 128) * 4 + 4)], [207, 22, 22, 171]);
  const lastOffset = ((4 * 256 + 180) * 3072 + 11 * 256 + 60) * 4; // 第59帧测试像素在图集中真实偏移
  assert.deepEqual([...decoded.data.subarray(lastOffset, lastOffset + 4)], [131, 37, 29, 127]);
});
