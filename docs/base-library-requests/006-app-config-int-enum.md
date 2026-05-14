# Esp32Base App Config 支持整型枚举字段

## 背景

Esp32_Faucet 接入 Esp32Base `/esp32base/app-config` 时，已有业务配置都保存在 `faucet_cfg` namespace。项目要求 App Config 字段直接绑定现有 namespace/key，不能迁移、清空或隐式改变已有 NVS 数据类型。

## 现状问题

Esp32Base 当前 `Esp32BaseAppConfig::addEnum()` 只支持字符串枚举：读取使用 `Esp32BaseConfig::getStr()`，保存使用 `setStr()`。但本项目已有枚举参数使用整型存储，例如：

- `faucet_cfg.pN_type`：`0 = Volume`，`1 = Time`

如果业务项目直接用 `addEnum()` 绑定这些 key，会把原本的 int NVS entry 改写为 string entry，后续 `ConfigStore::getInt()` 读取语义不稳定，也会让已有配置结构发生隐式变化。

## 复现方式

1. 在业务项目中注册：
   `Esp32BaseAppConfig::addEnum({"presets", "faucet_cfg", "p0_type", ...})`
2. 页面保存后，Esp32Base 调用 `Esp32BaseConfig::setStr("faucet_cfg", "p0_type", "1")`。
3. 业务现有配置读取路径仍调用 `Esp32BaseConfig::getInt("faucet_cfg", "p0_type", 0)`。

## 影响范围

所有已有配置中用 int 表达枚举、但希望通过 App Config 以下拉框编辑的业务项目都会遇到同类问题。

## 期望能力

请在 Esp32Base App Config 中增加整型枚举字段能力，例如独立的 `addIntEnum()` 或在 enum field 中明确声明存储类型。要求：

- 页面仍以下拉选项展示。
- 后端使用 `getInt()` / `setInt()` 读写。
- `ChangeCallback` 能提供旧值、新值和 `restartRequired`。
- 保存逻辑继续保持只写变化字段。
- 不要求业务项目临时转换 NVS 类型。

## 为什么不在业务项目绕开

在 Esp32_Faucet 内把 int enum 改成 string enum 会改变已有持久化结构；为保护用户配置，不应在业务项目里静默改变 key 类型或额外维护镜像 key。
