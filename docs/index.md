# 📚 XISF Shell Extensions Documentation

Welcome! This is your central hub for everything XISF Shell Extensions — from installation to advanced development.

---

## 🎯 Quick Navigation

| 🧑‍💻 **For Users** | 👨‍💼 **For Developers** | 📖 **Reference** |
|---|---|---|
| [Getting Started](#-getting-started) | [Developer Guide](#-developer-documentation) | [Property Reference](#-technical-reference) |
| [Installation](#installation-setup) | [Architecture](#architecture--design) | [Troubleshooting](#troubleshooting--diagnostics) |
| [Using Handlers](#using-the-handlers) | [Building & Testing](#development-workflow) | [API Reference](#api--registry-reference) |

---

## 🚀 Getting Started

### **New to XISF Shell Extensions?**

Start here if you've just discovered this project or want a quick overview:

- **[Getting Started](getting-started.md)** (5 min read)  
  Your first experience — what you get, what to do next, common first tasks

- **[Installation Guide](installation-guide.md)** (10 min read)  
  System requirements, MSI installation, developer installation, verification

**Quick path:** Install → [Getting Started](getting-started.md) → [Handlers Overview](user-guide/handlers-overview.md)

### **Just Installed? Three Quick Steps**

1. Restart Windows Explorer to activate handlers
2. Browse to an XISF file → Check the Details pane for metadata
3. Open [Settings](user-guide/settings-reference.md) to enable optional features (catalogs, search indexing)

---

## 👥 User Documentation

### Installation & Setup
- **[Installation Guide](installation-guide.md)**  
  Download, system requirements, install via MSI or developer method, verification

- **[Getting Started](getting-started.md)**  
  First-time walkthrough, what each handler does, enabling features

### Using the Handlers
- **[Handlers Overview](user-guide/handlers-overview.md)**  
  What handlers are, how they work, enable/disable instructions, registry locations

- **[Property Metadata](user-guide/property-metadata.md)**  
  What metadata is displayed, supported properties, catalog-specific fields

- **[Preview & Thumbnails](user-guide/preview-thumbnails.md)**  
  Thumbnail generation, preview display, performance tuning

- **[Windows Search Integration](user-guide/search-indexing.md)**  
  Enabling search, indexing XISF files, search filter syntax

### Managing Catalogs & Features
- **[Catalog Management](user-guide/catalog-management.md)**  
  Installing astronomy catalogs, updating, configuration, NGC/IC/Sharpless

- **[Feature Tiers Explained](features/feature-tiers.md)**  
  Free vs. premium features, catalog dependencies, enabling features

- **[Settings Reference](user-guide/settings-reference.md)**  
  All configuration options, registry keys, environment variables

---

## 🎨 Feature Reference

All features, explained for both users and developers:

- **[Pixel Statistics](features/pixel-statistics.md)**  
  Image histogram analysis, pixel counts, dynamic range, noise profiles

- **[Computed Properties](features/computed-properties.md)**  
  Calculated fields (e.g., focal ratio, angular size), formula reference

- **[Constellation Mapping](features/constellation-mapping.md)**  
  Sky coordinate-to-constellation conversion, supported catalogs

- **[Preview Handler Deep Dive](features/preview-handler-deep-dive.md)**  
  Technical design, caching, performance, scaling algorithms

- **[Telemetry & ETW Tracing](features/telemetry-etw.md)**  
  What's collected, privacy, ETW trace capture, diagnostic sessions

---

## 👨‍💻 Developer Documentation

### Getting Started for Developers
- **[Contributing](developer-guide/contributing.md)**  
  Contributing guidelines, code style, PR process, community expectations

- **[Building from Source](developer-guide/building.md)**  
  Build environment, dependencies, build commands, output artifacts

### Architecture & Design
- **[Architecture Overview](developer-guide/architecture.md)**  
  System design, component relationships, handler architecture, data flow

- **[Component Relationships](features/preview-handler-deep-dive.md#component-design)**  
  How components interact (in preview handler guide)

### Deep Dives
- **[Property Handler Implementation](developer-guide/property-handler-impl.md)**  
  Handler registration, property queries, XISF parsing, performance

- **[Search Filter Details](developer-guide/property-handler-impl.md#search-integration)**  
  Search indexing implementation (in property handler guide)

### Development Workflow
- **[Testing Guide](developer-guide/testing.md)**  
  Testing strategy, unit tests, integration tests, running test suites

- **[Debugging Guide](developer-guide/debugging.md)**  
  Debugging handlers in Visual Studio, ETW tracing, breakpoint strategies

- **[Telemetry & ETW Tracing](features/telemetry-etw.md)**  
  Capturing traces, analyzing ETW events, diagnostic logging

---

## 📋 Technical Reference

Complete technical specifications for integrators and advanced users:

### **Property Mapping**
- **[Property Mapping Reference](reference/property-mapping.md)**  
  XISF properties ↔ Windows properties, supported types, read-only fields

### **Handler Technical Details**
- **[Handlers Technical Reference](reference/handlers-technical.md)**  
  Handler lifecycle, COM interfaces, Windows Explorer integration points

- **[Catalog Specifications](reference/catalog-spec.md)**  
  Catalog format, adding custom catalogs, NGC/IC/Sharpless schema

### **API & Registry**
- **[API Reference](reference/api-reference.md)**  
  Public APIs, COM interfaces, registry structures, programmatic access

### **Troubleshooting & Diagnostics**
- **[Troubleshooting Guide](reference/troubleshooting.md)**  
  Common issues, diagnostic steps, FAQ, error codes and solutions

- **[Upgrade Guide](migration/upgrade-guide.md)**  
  Version migration, breaking changes, upgrade procedures

---

## 🔍 Quick Links by Use Case

**"I'm new to deep-sky image processing"**  
→ Start with [Installation](installation-guide.md) → [Getting Started](getting-started.md) → [Catalog Management](user-guide/catalog-management.md)

**"I have an error or issue"**  
→ Check [Troubleshooting](reference/troubleshooting.md) → [Debugging Guide](developer-guide/debugging.md)

**"I want to add a new property"**  
→ Read [Property Handler Implementation](developer-guide/property-handler-impl.md) → [Property Mapping](reference/property-mapping.md)

**"I want to contribute code"**  
→ Start with [Contributing](developer-guide/contributing.md) → [Architecture](developer-guide/architecture.md) → [Building](developer-guide/building.md)

**"I need to understand handler design"**  
→ [Architecture Overview](developer-guide/architecture.md) → [Handlers Technical Reference](reference/handlers-technical.md)

**"I want to use Windows Search with XISF"**  
→ [Windows Search Integration](user-guide/search-indexing.md) → [Property Mapping](reference/property-mapping.md)

---

## 📂 Documentation Structure

```
docs/
├── index.md                                    # Main entry point (you are here)
├── installation-guide.md                       # Installation & setup
├── getting-started.md                          # First-time user guide
│
├── user-guide/                                 # User documentation
│   ├── handlers-overview.md                    # Handler concepts & usage
│   ├── property-metadata.md                    # What's displayed
│   ├── preview-thumbnails.md                   # Previews & thumbnails
│   ├── search-indexing.md                      # Windows Search integration
│   ├── catalog-management.md                   # Installing catalogs
│   └── settings-reference.md                   # All configuration options
│
├── features/                                   # Feature deep dives
│   ├── feature-tiers.md                        # Free vs. premium
│   ├── pixel-statistics.md                     # Image analysis
│   ├── computed-properties.md                  # Calculated fields
│   ├── constellation-mapping.md                # Sky coordinates
│   ├── preview-handler-deep-dive.md            # Technical design
│   └── telemetry-etw.md                        # Telemetry & tracing
│
├── developer-guide/                            # Developer documentation
│   ├── contributing.md                         # How to contribute
│   ├── building.md                             # Build instructions
│   ├── architecture.md                         # System design
│   ├── property-handler-impl.md                # Property handler deep dive
│   ├── testing.md                              # Testing guide
│   └── debugging.md                            # Debugging techniques
│
├── reference/                                  # Technical references
│   ├── property-mapping.md                     # XISF ↔ Windows mapping
│   ├── handlers-technical.md                   # Handler specifications
│   ├── catalog-spec.md                         # Catalog format
│   ├── api-reference.md                        # Public API docs
│   └── troubleshooting.md                      # Issues & solutions
│
└── migration/                                  # Upgrades & migration
    └── upgrade-guide.md                        # Version migration
```

---

## 💡 Tips for Using This Documentation

- **Visual hierarchy:** Main sections use H2 (`##`), topics use H3 (`###`)
- **Cross-references:** Use the quick links above to jump between sections
- **Search:** Use your browser's find feature (Ctrl+F) or GitHub's search
- **Mobile-friendly:** All pages are optimized for mobile viewing
- **Estimated times:** Look for **(N min read)** badges for longer docs

---

## 🆘 Getting Help

**Can't find what you need?**

1. **[Troubleshooting Guide](reference/troubleshooting.md)** — Most common issues covered
2. **[Architecture Overview](developer-guide/architecture.md)** — Understand how things work
3. **[API Reference](reference/api-reference.md)** — Technical details
4. **GitHub Issues** — Search existing issues or create a new one

---

## 📦 Complete Documentation Inventory

**Total Documentation Files:** 32  
**Organized in:** 5 directories + root  
**All topics covered:** ✅ User guides ✅ Developer guides ✅ Technical reference ✅ Troubleshooting

### By Category
- **User Guides:** 6 files (installation, getting started, handlers, metadata, previews, search, catalogs, settings)
- **Features:** 6 files (feature tiers, pixel statistics, computed properties, constellation mapping, preview handler, telemetry)
- **Developer Guides:** 6 files (contributing, building, architecture, property handler implementation, testing, debugging)
- **Technical Reference:** 5 files (property mapping, handlers technical, catalog specs, API reference, troubleshooting)
- **Migration:** 1 file (upgrade guide)
- **Quick Reference:** Root-level docs (legacy versions available)

### No Orphaned Docs
Every documentation file is linked from this index or appears in the structure diagram above.

---

**Last Updated:** 2025  
**Documentation Version:** 2.0  
**XISF Shell Extensions** — Deep-sky image catalog integration for Windows Explorer

---

## 📝 Legacy Documentation

These files have been migrated to the new structure but are available for reference:

- **[handlers-overview.md](handlers-overview.md)** → See [Handlers Overview (User Guide)](user-guide/handlers-overview.md)
- **[property-handler.md](property-handler.md)** → See [Property Mapping Reference](reference/property-mapping.md)
- **[preview-handler.md](preview-handler.md)** → See [Preview Handler Deep Dive](features/preview-handler-deep-dive.md)
- **[debugging.md](debugging.md)** → See [Debugging Guide](developer-guide/debugging.md)
- **[telemetry.md](telemetry.md)** → See [Telemetry & ETW Tracing](features/telemetry-etw.md)
