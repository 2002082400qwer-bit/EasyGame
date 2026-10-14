'use strict';
const fs = require('node:fs/promises'); // 帧清单和关键帧读取接口
const path = require('node:path'); // 素材和工程路径解析
const sharp = require('./deps.cjs').loadDependency('sharp'); // 将现有PNG排版为检查用对照图，不绘制或补充姿势
const root = path.resolve(__dirname, '../../图片/游戏素材/diluc-redrawn-60'); // 本轮重绘素材根目录
const seam = process.argv.includes('--seams'); // 是否检查循环帧59到00的接缝
const indices = seam ? [56,57,58,59,0,1,2,3] : [0,12,24,36,48,59]; // 每组实际需要检查的帧序号
const columns = indices.length; // 检查图每行的帧列数
const perPage = seam ? 5 : 6; // 每页检查的动作数量

/** 制作关键帧对照图；ids为动作标识列表，空列表则读取全部现有组。 */
async function main(ids) {
  if (!ids.length) {
    const files = (await fs.readdir(path.join(root, 'metadata'))).filter(file => file.endsWith('.json')); // file为实际元数据文件名
    for (const file of files) { // 当前需要按循环属性筛选的组
      const meta = JSON.parse(await fs.readFile(path.join(root, 'metadata', file), 'utf8')); // 当前真实组定义
      if (!seam || meta.loop) ids.push(meta.id);
    }
  }
  await fs.mkdir(path.join(root, 'qa'), { recursive: true });
  const pages = []; // 对照图页面与每行动作标识记录
  for (let start = 0; start < ids.length; start += perPage) { // 当前检查页面起点
    const pageIds = ids.slice(start, start + perPage); // 本页按行排列的动作标识
    const composites = []; // 只裁取和排版既有PNG的图层
    for (let row = 0; row < pageIds.length; row++) { // 当前对照图动作行号
      for (let column = 0; column < indices.length; column++) { // 当前对照图帧列号
        const image = path.join(root, 'frames', pageIds[row], String(indices[column]).padStart(2, '0') + '.png'); // 当前实际已导出PNG
        composites.push({ input: image, left: column * 256, top: row * 280 + 24 });
      }
      const label = Buffer.from('<svg width="' + columns * 256 + '" height="24"><text x="12" y="18" font-size="15" font-family="sans-serif" fill="white">' + pageIds[row] + ' · frames ' + indices.join(' / ') + '</text></svg>'); // 仅供QA的标题标签，不改变正式素材
      composites.push({ input: label, left: 0, top: row * 280 });
    }
    const image = 'qa/' + (seam ? 'seams-' : 'keyframes-') + (pages.length + 1) + '.png'; // 当前检查页面相对路径
    await sharp({ create: { width: columns * 256, height: pageIds.length * 280, channels: 4, background: '#344052' } }).composite(composites).png().toFile(path.join(root, image));
    pages.push({ image, ids: pageIds, indices });
  }
  await fs.writeFile(path.join(root, seam ? 'qa/seams.json' : 'qa/keyframes.json'), JSON.stringify(pages, null, 2));
  console.log(JSON.stringify(pages));
}
main(process.argv.slice(2).filter(value => value !== '--seams')).catch(error => { console.error(error); process.exitCode = 1; }); // value为CLI参数，error为资源读取或排版失败原因
