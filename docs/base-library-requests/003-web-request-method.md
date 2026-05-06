# Esp32Base 请求：Web API 暴露当前请求方法

状态：已由 Esp32Base 完成，并已在本项目接入验证。

验证记录：

- 本项目已通过 `Esp32BaseWeb::isMethod(Esp32BaseWeb::METHOD_POST)` 在 `METHOD_ANY` 路由内区分 GET / POST。
- `/api/faucet/config`、`/api/faucet/presets`、`/api/faucet/calibration` 已使用同一路径分别支持读取和保存。
- Web 端仍不注册任何启动出水、暂停出水、继续出水或停止出水 API。
- `pio test -e native` 通过。
- `pio run -e esp32dev` 通过。

## 背景

本项目需要在 Web 端提供配置查看和配置保存 API，例如：

- `GET /api/faucet/config`：查看配置。
- `POST /api/faucet/config`：保存配置。
- `GET /api/faucet/presets`：查看预设。
- `POST /api/faucet/presets`：保存预设。
- `GET /api/faucet/calibration`：查看流量系数。
- `POST /api/faucet/calibration`：手动保存流量系数。

同时，本项目必须禁止任何远程出水控制 API。

Esp32Base 当前应用路由默认上限为 16 条。为了让页面和 API 都能注册，本项目把同一路径的 GET/POST 合并为 `Esp32BaseWeb::METHOD_ANY`，但业务 handler 需要知道当前请求到底是 GET 还是 POST。

## 当前 Esp32Base 能力缺口

`Esp32BaseWeb` 当前提供：

- `addRoute(path, method, handler)`
- `addPage(path, title, handler)`
- `addApi(path, handler)`
- `hasParam/getParam/getRequestBody`
- `sendJson/sendHtml/sendText`

内部实现中已经能取得 `g_server.method()`，但没有公开给应用层。

应用层如果无法区分请求方法，就不能安全地在同一路径下同时实现读取和写入；强行拆成独立 GET/POST 路由又容易超过默认应用路由数量上限。

## 期望 API

请为 `Esp32BaseWeb` 增加当前请求方法查询 API，例如：

```cpp
class Esp32BaseWeb {
public:
    static Method currentMethod();
    static bool isGet();
    static bool isPost();
};
```

建议行为：

- 在 handler 执行期间返回实际 HTTP 方法。
- 非 handler 上下文可以返回 `METHOD_ANY` 或最近一次方法，但需要文档说明。
- 至少区分 GET 与 POST。
- 不破坏现有 `addRoute`、`addApi` 和内置页面行为。

## 验收测试

在 Esp32Base 示例或测试应用中：

1. 注册 `Esp32BaseWeb::addRoute("/api/demo", Esp32BaseWeb::METHOD_ANY, handler)`。
2. GET 请求时，`Esp32BaseWeb::isGet()` 为 true，`isPost()` 为 false。
3. POST 请求时，`Esp32BaseWeb::isPost()` 为 true，`isGet()` 为 false。
4. 原有内置页面、应用页面、OTA、WiFi 页面仍能正常工作。

## 本项目约束

在基础库提供该能力前，本项目的 `METHOD_ANY` API 只返回只读数据，不在 handler 内猜测请求方法，也不注册远程出水控制接口。
