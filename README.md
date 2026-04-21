# Win11 XISF Shell Extension

Standalone Windows shell extension projects for `.xisf` files:

- `XISFPropertyHandler` (details pane + Windows Search metadata)
- `XISFPreviewHandler` (preview pane + thumbnail provider)

## Repository layout

```
Win11-XISF-Shell-Extension/
├── PropertyHandler/
│   ├── XISFPropertyHandler/
│   └── XISFPropertyHandlerTests/
├── PreviewHandler/
│   ├── XISFPreviewHandler/
│   └── XISFPreviewHandlerTests/
├── HelperScripts/
│   ├── Register-XISFHandler.ps1
│   ├── Set-XISFColumns.ps1
│   └── fetch-catalogs.ps1
├── docs/
│   ├── handlers-overview.md
│   ├── preview-handler.md
│   └── telemetry.md
└── Win11-XISF-Shell-Extension.sln
```

## Build

Open `Win11-XISF-Shell-Extension.sln` in Visual Studio (x64, Debug or Release).

## Registration

Use `HelperScripts\Register-XISFHandler.ps1` after building:

```powershell
.\HelperScripts\Register-XISFHandler.ps1 -Property Handler
.\HelperScripts\Register-XISFHandler.ps1 -Preview Handler
.\HelperScripts\Register-XISFHandler.ps1 -Property Handler,5
```

## Docs

- [`docs/handlers-overview.md`](docs/handlers-overview.md)
- [`docs/preview-handler.md`](docs/preview-handler.md)
- [`docs/telemetry.md`](docs/telemetry.md)

## License

MIT — see [LICENSE](LICENSE).
