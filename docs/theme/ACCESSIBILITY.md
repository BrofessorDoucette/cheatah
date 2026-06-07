# Accessibility of the cheatah docs 🐆

The generated documentation (a fixed **dark cheetah** theme) is held to
[WCAG 2.1](https://www.w3.org/TR/WCAG21/). This file records the decisions so the
docs stay accessible as they grow — when you add or change a color, check it
against the table below before committing.

## Success criteria we target

| Criterion | Level | Requirement | How we meet it |
|-----------|-------|-------------|----------------|
| **1.4.1** Use of Color | A | Color must not be the *only* way information is conveyed. | In-text links are **underlined**, not signaled by color alone (`cheetah.css`). Syntax tokens are also distinguishable by being inside code blocks. |
| **1.4.3** Contrast (Minimum) | AA | Text ≥ **4.5:1**; large text (≥18pt, or ≥14pt bold) ≥ **3:1**. | Met for every text color — see ratios below. |
| **1.4.6** Contrast (Enhanced) | AAA | Text ≥ **7:1**; large text ≥ **4.5:1**. | Body text and amber accents clear this; secondary/comment text clears AA and approaches AAA. |
| **1.4.11** Non-text Contrast | AA | UI components & graphics ≥ **3:1** vs adjacent colors. | The search box has an amber border; the focus ring is amber (≥3:1 on every surface). |
| **2.4.7** Focus Visible | AA | Keyboard focus must be visible. | An amber `outline` on `:focus-visible` for all links, buttons, inputs, tabs. |

## Verified contrast ratios

Measured against the page background `#1b1814` (and the fragment background
`#1f1c17` for code tokens):

| Role | Color | Ratio | Verdict |
|------|-------|------:|---------|
| Body text | `#ece4d6` | 14.0:1 | AAA |
| Amber accent / links / headings | `#f0a83e` | 8.7:1 | AAA |
| Secondary / muted text | `#a39a8b` | 6.4:1 | AA (≈AAA) |
| Code text | `#e7dfd0` on `#1f1c17` | ~12:1 | AAA |
| Keyword token | `#cc99cd` | ~7.0:1 | AAA |
| Keyword-type token | `#b8a6e0` | ~6.8:1 | AA |
| Keyword-flow token | `#f0922e` | ~7.3:1 | AAA |
| Literal token | `#8fd3a6` | ~8.5:1 | AAA |
| Comment token | `#a6a6a6` | ~6.6:1 | AA |

## Rules of thumb for future changes

- **New text color?** It must be **≥4.5:1** against its background (≥7:1 if you
  can). Use a contrast checker; don't eyeball it.
- **No white (or near-white) backgrounds.** Surfaces are warm charcoals; keep
  admonitions, tables, and code blocks dark (the theme variables already are).
- **Links inside prose stay underlined.** Don't remove the underline to "clean up"
  the look — that would reintroduce a 1.4.1 failure.
- **Keep a visible focus indicator.** Never set `outline: none` without an
  equally-visible replacement.
- **Diagrams** (Graphviz) are generated dark-aware via `HTML_COLORSTYLE = DARK`;
  if you change diagram colors, keep edges/labels ≥3:1 on the graph background.

Doxygen builds the site with **zero warnings**; keep it that way.
