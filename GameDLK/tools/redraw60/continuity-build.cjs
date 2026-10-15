'use strict';
const fs = require('node:fs/promises'); // 真实源图、时间线及候选资源文件接口
const path = require('node:path'); // 工程和候选目录路径接口
const sharp = require('./deps.cjs').loadDependency('sharp'); // 原图RGBA解码接口
const { detectFigures, packClip } = require('./pack.cjs'); // 真实人物计数和已验证的整组打包接口
const root = path.resolve(__dirname, '../../图片/游戏素材/diluc-redrawn-60'); // 正式素材根目录

/** 组装分阶段重绘候选；id为三方向跳跃或背面起身标识，publish决定是否正式发布。 */
async function build(id, publish) {
  const revision = JSON.parse(await fs.readFile(path.join(__dirname, 'continuity-plan.json'), 'utf8')); // 本次已确认的阶段和范围
  const rising = id === 'back_crouch_to_stand'; // 是否需要复核单向起身的真实姿态排序
  if (!revision.jumpGroups.includes(id) && !rising) throw new Error('UNKNOWN_PHASED_CLIP:' + id);
  const phases = rising ? ['low15', 'middle15', 'upper15', 'settle15'] : revision.jumpPhases; // 当前动作实际重绘阶段
  const perPhase = rising ? 15 : revision.framesPerPhase; // 当前阶段需要的不同新绘图数量
  const plan = JSON.parse(await fs.readFile(path.join(__dirname, 'full-generation-plan.json'), 'utf8')); // 原35组动作定义
  const clip = plan.clips.find(item => item.id === id); // item为当前需要重做的动作
  const target = publish ? root : path.join(root, '.staging', 'continuity-v3', id); // 默认只产出隔离候选，不覆盖正式资源
  const sources = []; // 实际新绘图、真实数量、选帧和尺度交接设置
  const heights = []; // 只供起身候选排序的实际归一轮廓高度，不能作为人体缩放依据
  await fs.mkdir(path.join(target, 'raw'), { recursive: true });
  for (const phase of phases) { // 当前10或15帧动作阶段
    const name = id + '-v3-' + phase + '.png'; // 当前新绘源图文件名
    const source = path.join(root, 'raw', name); // 已保存原始新绘图
    const decoded = await sharp(source).ensureAlpha().raw().toBuffer({ resolveWithObject: true }); // 当前源图真实RGBA
    const detected = detectFigures(decoded.data, decoded.info.width, decoded.info.height, null); // 真实人物数量和排序
    if (detected.figures.length < perPhase) throw new Error('INSUFFICIENT_PHASE:' + phase);
    const indices = Array.from({ length: perPhase }, (_, index) => Math.round(index * (detected.figures.length - 1) / (perPhase - 1))); // index为正式阶段帧序号，只选不同实绘人物
    for (const index of indices) heights.push((detected.figures[index].bottom - detected.figures[index].top + 1) / (decoded.info.height / detected.rows)); // 当前原图姿态的实际相对轮廓高度
    sources.push({ image: name, expected: detected.figures.length, indices, scaleMode: 'native', phase });
    if (!publish) await fs.copyFile(source, path.join(target, 'raw', name));
  }
  const layout = { sources }; // 实际来源和可复查时间线
  if (rising) layout.frameOrder = revision.riseFrameOrder ?? heights.map((height, index) => ({ height, index })).sort((first, second) => first.height - second.height).map(item => item.index); // 优先使用已复核时间线；height为姿态高度、index为原序号、first/second为待比较项、item为排序结果，均只重排实绘图
  const result = await packClip(target, clip, path.join(target, 'raw', sources[0].image), layout); // 内存清单与60帧资源一起验证、备份及事务提交
  console.log(JSON.stringify({ id, publish, target, frames: result.frameCount, verification: result.verification }));
}

/** CLI入口；args为动作标识列表以及可选的--publish，默认只创建候选。 */
async function main(args) {
  const publish = args.includes('--publish'); // 是否使用已人工复查的源图替换正式组
  const ids = args.filter(value => value !== '--publish'); // value为当前CLI参数
  if (!ids.length) throw new Error('用法：continuity-build.cjs <动作id> [--publish]');
  for (const id of ids) await build(id, publish); // 当前等待组装的动作标识
}
if (require.main === module) main(process.argv.slice(2)).catch(error => { console.error(error.message); process.exitCode = 1; }); // error为真实源图或导出失败原因
