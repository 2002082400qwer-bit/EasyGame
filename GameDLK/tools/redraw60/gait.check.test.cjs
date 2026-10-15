'use strict';
const test = require('node:test'); // Node原生回归测试接口
const assert = require('node:assert/strict'); // 严格行为断言
const fs = require('node:fs/promises'); // 独立透明PNG测试夹具读写
const path = require('node:path'); // 项目验证目录路径接口
const sharp = require('./deps.cjs').loadDependency('sharp'); // 合成空帧测试图片，不能用在正式动作中
const { check } = require('./gait.check.cjs'); // 实际脚部跨度检测入口

/* 回归空脚部误判：六十张透明测试帧必须报告空区域，不能以负宽度当作过腿通过。 */
test('透明空帧不能作为脚部收拢通过检查', async () => {
  const project = path.resolve(__dirname, '../..'); // 工程根目录
  const base = path.join(project, 'verification'); // 不提交的测试产物目录
  await fs.mkdir(base, { recursive: true });
  const root = await fs.mkdtemp(path.join(base, 'gait-empty-')); // 仅供本次测试的隔离资源根目录
  const frames = path.join(root, 'frames/side_slow_walk'); // 六十张空白测试PNG输出位置
  await fs.mkdir(frames, { recursive: true });
  const blank = await sharp({ create: { width: 256, height: 256, channels: 4, background: '#00000000' } }).png().toBuffer(); // 真实RGBA透明图，专门模拟无脚部像素的坏资源
  for (let frame = 0; frame < 60; ++frame) { // 当前测试帧序号
    await fs.writeFile(path.join(frames, String(frame).padStart(2, '0') + '.png'), blank);
  }
  await assert.rejects(check(root), /EMPTY_FOOT_REGION/);
});
