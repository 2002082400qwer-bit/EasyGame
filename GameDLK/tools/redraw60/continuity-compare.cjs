'use strict';
const fs = require('node:fs/promises'); // 原图集与对照文件读写接口
const path = require('node:path'); // 工程及对照路径接口
const assert = require('node:assert/strict'); // 对照GIF实际页数和时长验证
const sharp = require('./deps.cjs').loadDependency('sharp'); // 仅排版已有PNG，不补绘或插值姿势
const { previewDelays } = require('./pack.cjs'); // 一秒60帧的GIF量化延迟

/** 制作三方向跳跃同帧对照；无参数，左侧为修正前，右侧为正式新帧。 */
async function main() {
  const root = path.resolve(__dirname, '../../图片/游戏素材/diluc-redrawn-60'); // 素材根目录
  const ids = ['front_jump', 'side_jump', 'back_jump']; // 同时播放的三方向跳跃
  const atlases = []; // 预先解码的原版和新版图集，避免重复解码
  for (const id of ids) { // 当前对照动作
    for (const prefix of ['qa/continuity-before/', '']) { // 当前版本的相对目录
      atlases.push(await sharp(path.join(root, prefix, 'atlases', id + '.png')).ensureAlpha().raw().toBuffer({ resolveWithObject: true }));
    }
  }
  const buffers = []; // 60张同帧对照PNG，仅作为预览排版结果
  for (let index = 0; index < 60; index++) { // 当前播放帧序号
    const layers = []; // 三行动作的左右版本以及标题
    for (let panel = 0; panel < atlases.length; panel++) { // 当前动作和版本的面板序号
      const atlas = atlases[panel]; // 已解码的当前真实图集
      const row = Math.floor(panel / 2); // 当前动作所在行
      const column = panel % 2; // 旧版在左，新版在右
      const crop = await sharp(atlas.data, { raw: atlas.info }).extract({ left: index % 12 * 256, top: Math.floor(index / 12) * 256, width: 256, height: 256 }).png().toBuffer(); // 对应真实帧，不改变大小或像素姿势
      layers.push({ input: crop, left: column * 256, top: row * 280 + 24 });
      const title = Buffer.from('<svg width="256" height="24"><text x="8" y="17" font-size="13" font-family="sans-serif" fill="white">' + ids[row] + ' / ' + (column ? 'After' : 'Before') + ' / ' + String(index).padStart(2, '0') + '</text></svg>'); // 对照说明，不写入正式帧
      layers.push({ input: title, left: column * 256, top: row * 280 });
    }
    buffers.push(await sharp({ create: { width: 512, height: 840, channels: 4, background: '#344052' } }).composite(layers).png().toBuffer());
  }
  const destination = path.join(root, 'qa/continuity-comparison.gif'); // 正常一秒周期的三方向修正前后对照
  await sharp(buffers, { join: { animated: true } }).gif({ delay: previewDelays(1), loop: 0, effort: 5 }).toFile(destination);
  await fs.writeFile(path.join(root, 'qa/continuity-comparison-00.png'), buffers[0]);
  await fs.writeFile(path.join(root, 'qa/continuity-comparison-30.png'), buffers[30]);
  const actual = await sharp(destination, { animated: true }).metadata(); // 输出GIF的实际页数与延迟
  assert.equal(actual.pages, 60);
  assert.equal(actual.delay.reduce((sum, delay) => sum + delay, 0), 1000); // sum为累计毫秒，delay为每页真实延迟
  console.log(JSON.stringify({ comparison: destination, pages: actual.pages, durationMs: 1000 }));
}
main().catch(error => { console.error(error); process.exitCode = 1; }); // error为真实源图或排版检查失败原因
