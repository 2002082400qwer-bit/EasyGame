'use strict';
const fs = require('node:fs/promises'); // 新绘来源与动作计划的文件接口
const path = require('node:path'); // 工程与候选资源路径接口
const { packClip } = require('./pack.cjs'); // 已验证的六十帧独立导出和事务发布接口

/** 构建完整左右腿交替步态；publish为真时替换正式组，否则只生成隔离候选。 */
async function build(publish) {
  const root = path.resolve(__dirname, '../../图片/游戏素材/diluc-redrawn-60'); // 正式素材根目录
  const revision = JSON.parse(await fs.readFile(path.join(__dirname, 'gait-plan.json'), 'utf8')); // 四阶段真实新绘来源及提示词记录
  const plan = JSON.parse(await fs.readFile(path.join(__dirname, 'full-generation-plan.json'), 'utf8')); // 已确认三十五组动作定义
  const clip = plan.clips.find(item => item.id === revision.id); // item为需要修复的侧面慢走组
  const target = publish ? root : path.join(root, '.staging', 'gait-v4'); // 正式发布前用于人工检查的隔离目录
  await fs.mkdir(path.join(target, 'raw'), { recursive: true });
  if (!publish) {
    for (const source of revision.sources) { // 当前十五帧的新绘来源
      await fs.copyFile(path.join(root, 'raw', source.image), path.join(target, 'raw', source.image));
    }
  }
  const result = await packClip(target, clip, path.join(target, 'raw', revision.sources[0].image), { sources: revision.sources }); // 六十个不同真实人物完整导出
  console.log(JSON.stringify({ id: result.id, target, publish, frames: result.frameCount, verification: result.verification }));
}

if (require.main === module) build(process.argv.includes('--publish')).catch(error => { console.error(error); process.exitCode = 1; }); // error为缺帧、裁剪或事务发布错误
