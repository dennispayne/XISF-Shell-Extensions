# XISF Shell Extensions Documentation

Welcome to the comprehensive documentation for XISF Shell Extensions. This guide covers installation, usage, features, development, and troubleshooting.

## Quick Navigation

### For Users
- **[Installation Guide](installation-guide.md)** - Download, install, and configure the extensions
- **[Getting Started](getting-started.md)** - First-time user walkthrough and quick start
- **[User Guide](user-guide/handlers-overview.md)** - Detailed feature documentation

### For Feature Exploration
- **[Features Overview](features/feature-tiers.md)** - Browse all available features
- **[Pixel Statistics](features/pixel-statistics.md)** - Image analysis and statistics
- **[Computed Properties](features/computed-properties.md)** - Advanced property calculations
- **[Constellation Mapping](features/constellation-mapping.md)** - Astronomical observations
- **[Preview Handler Deep Dive](features/preview-handler-deep-dive.md)** - Technical details on preview/thumbnail handlers
- **[Telemetry & ETW](features/telemetry-etw.md)** - Telemetry and Event Tracing

### For Developers
- **[Architecture](developer-guide/architecture.md)** - System design and components
- **[Building](developer-guide/building.md)** - Build instructions and environment setup
- **[Contributing](developer-guide/contributing.md)** - Contribution guidelines
- **[Testing](developer-guide/testing.md)** - Testing strategy and procedures
- **[Debugging](developer-guide/debugging.md)** - Debugging techniques and tools

### Reference & Support
- **[Property Mapping Reference](reference/property-mapping.md)** - Complete property registry
- **[Handler Technical Reference](reference/handlers-technical.md)** - Technical specifications
- **[API Reference](reference/api-reference.md)** - Public API documentation
- **[Troubleshooting](reference/troubleshooting.md)** - Common issues and solutions
- **[Upgrade Guide](migration/upgrade-guide.md)** - Migration and upgrade information

## Documentation Structure

```
docs/
├── index.md                           # Main entry point (you are here)
├── installation-guide.md              # Installation and setup
├── getting-started.md                 # First-time user guide
├── user-guide/                        # Feature documentation
│   ├── handlers-overview.md
│   ├── property-metadata.md
│   ├── preview-thumbnails.md
│   ├── search-indexing.md
│   ├── catalog-management.md
│   └── settings-reference.md
├── features/                          # Feature deep dives
│   ├── pixel-statistics.md
│   ├── computed-properties.md
│   ├── constellation-mapping.md
│   ├── feature-tiers.md
│   ├── preview-handler-deep-dive.md
│   └── telemetry-etw.md
├── developer-guide/                   # Developer documentation
│   ├── architecture.md
│   ├── contributing.md
│   ├── building.md
│   ├── testing.md
│   ├── debugging.md
│   └── property-handler-impl.md
├── reference/                         # Technical references
│   ├── property-mapping.md
│   ├── handlers-technical.md
│   ├── catalog-spec.md
│   ├── api-reference.md
│   └── troubleshooting.md
└── migration/                         # Migration and upgrades
    └── upgrade-guide.md
```

## Documentation Goals

This documentation aims to provide:
- **Clear, practical guidance** for all user levels
- **Complete technical references** for developers
- **Comprehensive troubleshooting** resources
- **Architecture and design** documentation for contributors

## Getting Help

- Check the [Troubleshooting Guide](reference/troubleshooting.md) for common issues
- Review the [FAQ](reference/troubleshooting.md) section
- Consult the [API Reference](reference/api-reference.md) for technical details

---

**Last Updated:** [To be maintained]  
**Version:** XISF Shell Extensions Documentation v1.0
