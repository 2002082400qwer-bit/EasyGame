'use strict';
const fs = require('node:fs/promises'); // 离线预览文件与验收报告读写
const path = require('node:path'); // 本地浏览器和工程路径解析
const { pathToFileURL } = require('node:url'); // Windows中文本地路径的安全URL转换
const assert = require('node:assert/strict'); // 实际浏览器行为断言
const { chromium } = require('./deps.cjs').loadDependency('playwright'); // 已安装浏览器的无界面自动检查接口
const sharp = require('./deps.cjs').loadDependency('sharp'); // 实际浏览器截图像素读取，不绕过本地画布来源限制

/** 在实际浏览器检查离线播放器；无参数，覆盖35个图集、逐帧与非循环末帧行为。 */
async function main() {
  const root = path.resolve(__dirname, '../../图片/游戏素材/diluc-redrawn-60'); // 本轮完整素材目录
  const report = JSON.parse(await fs.readFile(path.join(root, 'verification-report.json'), 'utf8')); // 实际PNG和图集检查清单
  assert.equal(report.verifiedGroups, 35);
  const executablePath = 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe'; // 当前电脑实际存在的浏览器位置
  const browser = await chromium.launch({ executablePath, headless: true }); // 隐藏窗口的浏览器实例
  try {
    const page = await browser.newPage({ viewport: { width: 1100, height: 980 } }); // 当前离线预览检查页面
    const errors = []; // 实际脚本错误与资源失败记录
    page.on('pageerror', error => errors.push(error.message)); // error为页面运行错误
    page.on('requestfailed', request => errors.push(request.url() + ':' + request.failure().errorText)); // request为未加载成功的本地资源
    await page.goto(pathToFileURL(path.join(root, 'preview.html')).href);
    assert.equal(await page.locator('#clip option').count(), 35);
    for (let index = 0; index < report.verified.length; index++) { // 当前动作选择序号
      const clip = report.verified[index]; // 当前已经通过PNG检查的真实动作
      await page.selectOption('#clip', String(index));
      await page.waitForFunction(id => active.id === id && atlas !== null, clip.id); // id为预期加载完成的动作标识
      const screenshot = await page.locator('#sprite').screenshot(); // 实际渲染画布截图，file网址不允许直接getImageData
      const pixels = await sharp(screenshot).ensureAlpha().raw().toBuffer(); // 浏览器真实显示的RGBA像素
      const visible = pixels.some((value, index) => index % 4 === 0 && value > 90 && pixels[index + 1] < 80); // value为红通道，index为像素起点；背景格不满足红发颜色条件
      assert.equal(visible, true, '预览画布为空:' + clip.id);
    }
    const transition = report.verified.findIndex(clip => clip.id === 'side_crouch_to_dash_to_stand'); // 明确要求末状态站立的非循环动作序号
    await page.selectOption('#clip', String(transition));
    await page.waitForFunction(() => atlas !== null);
    await page.click('#play');
    await page.waitForFunction(() => frame === 59 && playing === false, null, { timeout: 5000 });
    assert.equal(await page.locator('#number').textContent(), '59 / 59');
    await page.screenshot({ path: path.join(root, 'qa/ui-preview.png'), fullPage: true });
    await page.locator('#frame').evaluate(element => { element.value = '44'; element.dispatchEvent(new Event('input')); }); // element为实际帧号滑块
    assert.equal(await page.locator('#number').textContent(), '44 / 59');
    const idle = report.verified.findIndex(clip => clip.id === 'front_idle'); // 循环待机动作序号
    await page.selectOption('#clip', String(idle));
    await page.waitForFunction(() => atlas !== null);
    await page.check('#seam');
    assert.equal(await page.locator('#number').textContent(), '56 / 59');
    await page.click('#play');
    await page.waitForFunction(() => [0,1,2,3].includes(frame), null, { timeout: 3000 });
    assert.equal(errors.length, 0, JSON.stringify(errors));
    const result = { passed: true, clipsLoaded: 35, canvasDrawVerified: true, transitionStopsAt59: true, manualFrameVerified: true, seamWrapVerified: true, errors }; // 实际浏览器检查结果
    await fs.writeFile(path.join(root, 'qa/preview-check.json'), JSON.stringify(result, null, 2));
    console.log(JSON.stringify(result));
  } finally { await browser.close(); }
}
main().catch(error => { console.error(error); process.exitCode = 1; }); // error为实际浏览器检查失败原因
