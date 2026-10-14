'use strict';
const fs = require('node:fs/promises'); // 正式素材与元数据读取接口
const path = require('node:path'); // 工程与素材路径解析
const crypto = require('node:crypto'); // PNG及像素真实性摘要
const sharp = require('./deps.cjs').loadDependency('sharp'); // PNG与动画实际解码接口
const { frameDifference, detectFigures } = require('./pack.cjs'); // 预乘差异与源图独立人物检查
const project = path.resolve(__dirname, '../..'); // 本工具所属工程根目录
const root = path.join(project, '图片/游戏素材/diluc-redrawn-60'); // 本轮正式素材目录

/** 计算真实字节摘要；data是PNG文件或RGBA像素。 */
function hash(data) { return crypto.createHash('sha256').update(data).digest('hex'); }

/** 断言交付不变量；condition是检查结果，message说明失败的具体资源。 */
function requireValid(condition, message) { if (!condition) throw new Error(message); }

/** 检查完整动作；clip是需求计划，返回实际数量、首尾差异及播放信息。 */
async function auditClip(clip) {
  const meta = JSON.parse(await fs.readFile(path.join(root, 'metadata', clip.id + '.json'), 'utf8')); // 当前正式帧清单
  const files = (await fs.readdir(path.join(root, 'frames', clip.id))).filter(file => file.endsWith('.png')); // 实际PNG文件列表
  requireValid(files.length === 60 && meta.frames.length === 60, 'PNG数量不是60');
  const atlas = await sharp(path.join(root, meta.atlas.image)).ensureAlpha().raw().toBuffer({ resolveWithObject: true }); // 正式图集真实RGBA
  requireValid(atlas.info.width === 3072 && atlas.info.height === 1280 && atlas.info.channels === 4, '图集尺寸或格式异常');
  const pixels = []; // 已验证的真实帧像素
  const hashes = new Set(); // 独立帧的像素摘要集合
  for (const frame of meta.frames) { // 当前需要与清单和图集对照的帧
    const bytes = await fs.readFile(path.join(root, frame.image)); // 当前真实PNG字节
    const decoded = await sharp(bytes).raw().toBuffer({ resolveWithObject: true }); // 当前真实PNG像素和尺寸
    requireValid(decoded.info.width === 256 && decoded.info.height === 256 && decoded.info.channels === 4, '帧尺寸或RGBA异常:' + frame.index);
    requireValid(hash(bytes) === frame.sha256 && hash(decoded.data) === frame.pixelSha256, '帧摘要不一致:' + frame.index);
    let visible = 0; // 非透明像素总数
    for (let y = 0; y < 256; y++) { // 当前输出像素行
      for (let x = 0; x < 256; x++) { // 当前输出像素列
        const offset = (y * 256 + x) * 4; // 当前帧像素起点
        const alpha = decoded.data[offset + 3]; // 当前帧透明度
        if (alpha > 0) visible++;
        requireValid(!(x === 0 || x === 255 || y === 0 || y === 255) || alpha === 0, '输出轮廓触边:' + frame.index);
        const atlasOffset = ((y + frame.rect.y) * atlas.info.width + x + frame.rect.x) * 4; // 图集中的对应像素起点
        requireValid(decoded.data.readUInt32LE(offset) === atlas.data.readUInt32LE(atlasOffset), '图集像素不匹配:' + frame.index);
      }
    }
    requireValid(visible > 200 && visible < 256 * 256 * 0.8, '透明底或人物异常:' + frame.index);
    hashes.add(hash(decoded.data)); pixels.push(decoded.data);
  }
  requireValid(hashes.size === 60, '实际像素存在重复帧');
  for (const source of meta.sources) { // 当前来源原图及实际选帧记录
    requireValid(hash(await fs.readFile(path.join(root, source.image))) === source.sha256, '源图摘要不一致:' + source.image);
    requireValid(new Set(source.selectedIndices).size === source.selectedIndices.length, '来源帧被重复选择');
    const decoded = await sharp(path.join(root, source.image)).ensureAlpha().raw().toBuffer({ resolveWithObject: true }); // 源图实际RGBA
    const detected = detectFigures(decoded.data, decoded.info.width, decoded.info.height, source.actualFigures); // 拒绝相邻人物被当作单帧或实际来源帧数变化
    requireValid(detected.rowCounts.join(',') === source.rowCounts.join(','), '源图行顺序发生变化');
  }
  const gif = await sharp(path.join(root, meta.preview.image), { animated: true }).metadata(); // GIF实际页数和帧延迟
  requireValid(gif.pages === 60 && gif.width === 256 && gif.pageHeight === 256, 'GIF缺帧或尺寸异常');
  const milliseconds = gif.delay.reduce((sum, delay) => sum + delay, 0); // GIF解码后的总毫秒，sum为累计值，delay为单帧延迟
  requireValid(milliseconds === Math.round(meta.durationSeconds * 1000), 'GIF时长异常:' + milliseconds);
  return { id: clip.id, action: clip.action, direction: clip.direction, from: clip.from, to: clip.to, endState: clip.endState, loop: meta.loop, frames: 60, distinctPixelFrames: hashes.size, pngVerified: true, atlasVerified: true, previewMilliseconds: milliseconds, suggestedFps: meta.suggestedFps, seamDifference: frameDifference(pixels[59], pixels[0]), seamMetricPassed: meta.verification.seamMetricPassed, visualContinuityVerified: meta.verification.visualContinuityVerified, atlas: meta.atlas.image, preview: meta.preview.image };
}

/** 全部动作验收并写入离线预览数据；无参数，按工程中已确认35组计划检查。 */
async function main() {
  const plan = JSON.parse(await fs.readFile(path.join(__dirname, 'full-generation-plan.json'), 'utf8')); // 完整35组用户确认计划
  const verified = []; // 实际通过结构校验的资源组
  const failures = []; // 缺失或有实质错误的组
  for (const clip of plan.clips) { // 当前计划动作
    try { verified.push(await auditClip(clip)); }
    catch (error) { failures.push({ id: clip.id, error: error.message }); } // error为实际资源校验失败原因
  }
  const report = { generatedAt: new Date().toISOString(), expectedGroups: plan.clips.length, verifiedGroups: verified.length, verifiedPngs: verified.length * 60, verified, failures, loopSeamsToReview: verified.filter(clip => clip.loop && !clip.seamMetricPassed).map(clip => clip.id) }; // clip为尚需人工判断的循环断点
  await fs.writeFile(path.join(root, 'verification-report.json'), JSON.stringify(report, null, 2));
  await fs.writeFile(path.join(root, 'bundle.js'), 'window.SPRITE_BUNDLE = ' + JSON.stringify(report, null, 2) + ';\n');
  console.log(JSON.stringify({ groups: report.verifiedGroups, pngs: report.verifiedPngs, failures, loopSeamsToReview: report.loopSeamsToReview }));
  if (failures.length) process.exitCode = 1;
}

if (require.main === module) main().catch(error => { console.error(error); process.exitCode = 1; }); // error为读取计划或保存报告失败原因
module.exports = { auditClip };
