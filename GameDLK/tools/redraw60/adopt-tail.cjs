'use strict';
const fs = require('node:fs/promises'); // 已生成修正段及来源清单的读写接口
const path = require('node:path'); // 工程与源图路径解析
const sharp = require('./deps.cjs').loadDependency('sharp'); // 实际人物计数需要的RGBA解码
const { detectFigures, packClip } = require('./pack.cjs'); // 检查实绘帧与整组事务打包

/** 接纳已有新绘末段；id为动作标识，source为内置工具实际生成路径。 */
async function main(id, source) {
  const root = path.resolve(__dirname, '../../图片/游戏素材/diluc-redrawn-60'); // 正式素材根目录
  const plan = JSON.parse(await fs.readFile(path.join(__dirname, 'full-generation-plan.json'), 'utf8')); // 已确认的动作定义
  const clip = plan.clips.find(item => item.id === id); // 当前修正动作，item为计划记录
  if (!clip) throw new Error('UNKNOWN_CLIP');
  const meta = JSON.parse(await fs.readFile(path.join(root, 'metadata', id + '.json'), 'utf8')); // 修正前正式组的真实来源
  const first = meta.sources[0]; // 保留前45张新绘帧的原图来源
  const decoded = await sharp(source).ensureAlpha().raw().toBuffer({ resolveWithObject: true }); // 已有末段真实源图
  const figures = detectFigures(decoded.data, decoded.info.width, decoded.info.height, null).figures; // 末段实际可数人物
  if (figures.length < 15 || first.selectedIndices.length < 45) throw new Error('SOURCE_FRAMES_NOT_ENOUGH');
  const name = id + '-repair15-final.png'; // 正式使用的修正末段新源图名称
  await fs.copyFile(source, path.join(root, 'raw', name));
  const layout = { sources: [{ image: first.image.replace(/^raw\//, ''), expected: first.actualFigures, indices: first.selectedIndices.slice(0,45) }, { image: name, expected: figures.length, indices: Array.from({ length:15 }, (_, index) => Math.round(index * (figures.length - 1) / 14)) }] }; // index为真实末段帧选择序号
  await fs.writeFile(path.join(root, 'raw', id + '-source-layout.json'), JSON.stringify(layout, null, 2));
  await fs.writeFile(path.join(root, 'raw', id + '-repair15-final.json'), JSON.stringify({ source, method: 'built-in image_gen: first fifteen-frame invocation of repairTail', actualFigures: figures.length, refs: ['三视图.png', 'frames/' + id + '/44.png'], note: '末段已重新绘制，禁止重启整段动作；完整调用模板见tools/redraw60/repair-runner.js.txt' }, null, 2));
  const result = await packClip(root, clip, path.join(root, 'raw', id + '-full.png')); // 60张不同帧的完整修正输出
  console.log(JSON.stringify({ id, frameCount: result.frameCount, verification: result.verification }));
}
main(process.argv[2], process.argv[3]).catch(error => { console.error(error.message); process.exitCode = 1; }); // error为接纳或验证失败原因
