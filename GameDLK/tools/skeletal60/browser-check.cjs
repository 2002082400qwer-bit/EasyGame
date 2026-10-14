'use strict';
const fs = require('node:fs/promises'); // 保存浏览器截图和检查结果
const path = require('node:path'); // 工程与运行时依赖路径
const { pathToFileURL } = require('node:url'); // 将中文本地路径转换为浏览器地址
let playwright; // 当前可用的浏览器自动化依赖
try { playwright = require('playwright'); }
catch { playwright = require(path.join(path.dirname(process.execPath), '..', 'node_modules', 'playwright')); }

/** 实际打开离线对比页，检查旧新版切换、逐帧和骨骼，并保存截图；无参数。 */
async function main() {
  const output = path.resolve(__dirname, '../../图片/游戏素材/骨骼拆分方式'); // 用户指定的成品目录
  const browser = await playwright.chromium.launch({ channel: 'msedge', headless: true }); // 后台测试用Edge，不打开用户窗口
  const page = await browser.newPage({ viewport: { width: 1280, height: 1120 } }); // 足以显示完整人物和进度的画布
  const errors = []; // 浏览器运行异常
  const scenes = []; // 已检查的动作与帧号
  page.on('pageerror', error => errors.push(error.message)); // error为页面脚本异常
  try {
    await page.goto(pathToFileURL(path.join(output, 'preview.html')).href);
    await page.waitForFunction(() => document.getElementById('status').textContent.includes('60 FPS'));
    await page.locator('#play').click();
    if (await page.locator('#compare').inputValue() !== 'previous') throw new Error('默认对照没有选择修正前骨骼');
    for (const action of ['slow_walk', 'run', 'dash']) { // 当前检查的三类步态
      await page.locator('#action').selectOption(action);
      await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('修正后 '));
      await page.locator('#seek').evaluate(element => { // element为真实进度滑块，触发与用户拖动相同的输入事件
        element.value = '21';
        element.dispatchEvent(new Event('input', { bubbles: true }));
      });
      const status = await page.locator('#status').innerText(); // 当前界面实际显示的帧号
      if (!status.includes('修正后 21 / 59') || !status.includes('对照 21 / 59')) throw new Error(status);
      await page.screenshot({ path: path.join(output, 'previews', `browser_gait_${action}.png`), fullPage: true });
      await page.locator('#bones').check();
      await page.locator('#step').click();
      if (!(await page.locator('#status').innerText()).includes('修正后 22 / 59')) throw new Error('逐帧前进失败');
      await page.locator('#bones').uncheck();
      scenes.push({ action, compared_frame: 21, next_frame: 22, comparison: 'previous' });
    }
    await page.locator('#compare').selectOption('old');
    await page.waitForFunction(() => document.getElementById('comparisonTag').textContent.includes('8个姿势'));
    if (errors.length) throw new Error(errors.join('\n'));
    const report = { passed: true, page: 'preview.html', offline: true, scenes, old8_switch: true, page_errors: errors }; // 本次真实界面的验收结果
    await fs.writeFile(path.join(output, 'browser-verification.json'), JSON.stringify(report, null, 2), 'utf8');
    process.stdout.write(JSON.stringify(report) + '\n');
  } finally { await browser.close(); }
}
main().catch(error => { process.stderr.write(error.stack + '\n'); process.exitCode = 1; }); // error为需明确报告的浏览器启动或页面异常
