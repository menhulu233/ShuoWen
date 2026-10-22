# 说文安装 / 卸载

先打包绿色目录：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

打包后的 `dist\installer\` 也带一份相同脚本，可以直接随绿色包分发。

每用户安装（无需管理员权限）：

```powershell
powershell -ExecutionPolicy Bypass -File installer\install.ps1
```

- 安装到 `%LOCALAPPDATA%\Programs\ShuoWen`
- 创建开始菜单快捷方式
- 默认写入 `HKCU\...\Run` 开机自启；加 `-NoAutostart` 会同时把用户设置里的 `LaunchAtLogin` 置为 false
- 加 `-DesktopShortcut` 额外创建桌面快捷方式
- 加 `-NoLaunch` 只安装、不立即启动（适合脚本化部署）
- 写入 HKCU 的“应用和功能”条目，可从 Windows 设置直接卸载
- 用户数据仍在 `%APPDATA%\ShuoWen`，升级/卸载都不会删除

卸载：

```powershell
powershell -ExecutionPolicy Bypass -File installer\uninstall.ps1
```
