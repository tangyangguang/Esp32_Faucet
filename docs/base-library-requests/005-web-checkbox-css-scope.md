# Esp32Base 请求：修正 Web 全局 input 样式误作用到 checkbox

状态：Esp32Base 已修正。`sendHeader()` 的基础 CSS 已改为只作用于文本类 input，checkbox/radio 等非文本控件保持原生尺寸。本项目不再需要为基础库 input 污染写高优先级尺寸覆盖，仅保留业务表单自身布局样式。

请在 Esp32Base 项目中修正内置 Web 基础样式的 input 作用范围。当前 Esp32_Faucet 在配置页使用 checkbox 字段时发现，Esp32Base 的全局样式会把 checkbox 当成普通文本输入框处理，导致 checkbox 被设置为宽度 100%、padding、margin、border 等文本输入框样式，破坏业务页面表单排版。

## 背景

Esp32Base 的 `sendHeader()` 会输出基础页面样式。业务页面复用 `sendHeader()` / `sendFooter()` 后，再输出自己的业务表单。当前基础样式中存在类似规则：

```css
input:not([type=submit]):not([type=button]) { ... }
```

这个选择器会匹配 `input type="checkbox"`，从而把 checkbox 拉成普通文本输入框的尺寸。

## 现状问题

- checkbox 被基础库全局 input 样式影响。
- 业务页面必须写更高优先级的 CSS 才能把 checkbox 恢复正常。
- 这属于基础库页面框架的默认样式污染，不应由每个业务项目分别修补。
- 内置页面未来如果使用 checkbox、radio、file、range 等非文本控件，也可能遇到类似问题。

## 目标

Esp32Base 的默认输入框样式应只作用于文本类表单控件，不应影响 checkbox、radio、file、range、color 等控件。

## 期望行为

- `input type="checkbox"` 保持浏览器原生小勾选框尺寸。
- `input type="radio"` 保持浏览器原生单选框尺寸。
- 文本、数字、密码等常规输入框仍保持 Esp32Base 的统一宽度、边框和 padding。
- 业务页面复用 Esp32Base header/footer 时，不需要为 checkbox 写覆盖规则。

## 影响范围

- Esp32Base Web 全局 CSS。
- 内置 WiFi、OTA、Logs、Reboot 页面。
- 所有复用 `Esp32BaseWeb::sendHeader()` 的业务页面。

## 验证方式

- 新增或扩展示例页面，包含 text、number、password、checkbox、radio 等输入控件。
- 验证文本类输入框仍为统一宽度。
- 验证 checkbox/radio 不被拉伸，不产生异常 padding、border 或整行宽度。
- 在 Esp32_Faucet `/faucet/config` 页面验证“蜂鸣器”字段能够正常显示为紧凑勾选项。

## 为什么不应在 Esp32_Faucet 中临时绕开

这个问题来自 Esp32Base 输出的全局 CSS 选择器范围过宽。如果业务项目长期通过更高优先级 CSS 覆盖，会让每个项目都重复处理同一个基础库样式污染问题，也会让未来新增控件时继续踩坑。请在基础库中收敛默认 input 选择器范围。
