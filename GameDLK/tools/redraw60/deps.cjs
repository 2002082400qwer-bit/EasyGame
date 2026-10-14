'use strict';
const path = require('node:path'); // 工程依赖与Codex捆绑运行时路径解析

/** 加载依赖；name为模块名，优先npm本地安装，其次当前Node同级捆绑依赖。 */
function loadDependency(name) {
  try { return require(name); }
  catch (error) { // error为工程本地依赖读取错误
    if (error.code !== 'MODULE_NOT_FOUND') throw error;
    return require(require.resolve(name, { paths: [path.resolve(path.dirname(process.execPath), '..')] }));
  }
}
module.exports = { loadDependency };
