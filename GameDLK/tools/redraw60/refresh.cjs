'use strict';
const fs = require('node:fs/promises'); // 已导出动作元数据读取接口
const path = require('node:path'); // 工程路径解析接口
const { packClip } = require('./pack.cjs'); // 完整验证后进行暂存事务输出的打包函数

/** 更新已有组的图集与预览；无参数，保留原新绘来源和选帧清单。 */
async function main() {
  const project = path.resolve(__dirname, '../..'); // 工具所属工程根目录
  const root = path.join(project, '图片/游戏素材/diluc-redrawn-60'); // 本轮新绘素材目录
  const plan = JSON.parse(await fs.readFile(path.join(__dirname, 'full-generation-plan.json'), 'utf8')); // 全部已确认动作范围
  const present = new Set((await fs.readdir(path.join(root, 'metadata'))).map(file => file.replace(/\.json$/, ''))); // 启动时已导出的组，file为元数据文件名
  const failed = []; // 本次更新失败的组
  for (const clip of plan.clips.filter(item => present.has(item.id))) { // clip为当前已存在组，item为计划动作
    try {
      const result = await packClip(root, clip, path.join(root, 'raw', clip.id + '-full.png')); // 重新验证并完整更新的结果
      console.log(JSON.stringify({ id: result.id, frames: result.frameCount, seam: result.verification.seamMetricPassed }));
    } catch (error) { failed.push({ id: clip.id, error: error.message }); } // error为本组验证或更新失败原因
  }
  if (failed.length) { console.error(JSON.stringify(failed)); process.exitCode = 1; }
}
main().catch(error => { console.error(error); process.exitCode = 1; }); // error为目录或计划读取失败原因
