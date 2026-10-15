'use strict';
const fs = require('node:fs/promises'); // 真实帧及检查证据文件接口
const path = require('node:path'); // 正式或候选目录路径接口
const sharp = require('./deps.cjs').loadDependency('sharp'); // 实际PNG像素解码

/** 检查两次过腿确实收拢：root为素材目录；这里只验证脚部跨度，解剖左右腿由人工看图判断。 */
async function check(root) {
  const widths = []; // 六十帧脚部区域的不透明像素横向跨度
  for (let frame = 0; frame < 60; ++frame) { // 当前真实帧序号
    const rgba = await sharp(path.join(root, 'frames/side_slow_walk', String(frame).padStart(2, '0') + '.png')).ensureAlpha().raw().toBuffer(); // 当前256方形PNG原像素
    let left = 256; // 脚部区域最左核心像素
    let right = -1; // 脚部区域最右核心像素
    for (let y = 205; y < 236; ++y) { // 足部与脚踝所在图像行
      for (let x = 0; x < 256; ++x) { // 当前像素横坐标
        if (rgba[(y * 256 + x) * 4 + 3] >= 128) { left = Math.min(left, x); right = Math.max(right, x); }
      }
    }
    if (right < left) throw new Error('EMPTY_FOOT_REGION:' + frame);
    widths.push(right - left + 1);
  }
  const halves = [widths.slice(0, 30), widths.slice(30)]; // 左右两步分别检查，不能只摆动同一跨步
  const phases = halves.map(values => ({ min: Math.min(...values), max: Math.max(...values) })); // values为半周期的实际脚部跨度
  if (phases.some(phase => phase.min >= phase.max * 0.72)) throw new Error('STATIC_SPLIT_LEGS: 两个半周期都必须出现明显的过腿收拢姿态:' + JSON.stringify(phases)); // phase为半周期统计
  console.log(JSON.stringify({ id: 'side_slow_walk', frames: widths.length, phases, keyWidths: [0, 15, 30, 45, 59].map(frame => ({ frame, width: widths[frame] })), note: '跨度只能检测有无过腿，左右腿身份仍需人工检查' })); // frame为关键帧编号
  return { widths, phases };
}

if (require.main === module) check(path.resolve(process.argv[2] || path.join(__dirname, '../../图片/游戏素材/diluc-redrawn-60'))).catch(error => { console.error(error.message); process.exitCode = 1; }); // error为文件或步态验证错误
module.exports = { check };
