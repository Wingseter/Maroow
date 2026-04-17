# Design System: Technical Precision & Tonal Depth

## 1. Overview & Creative North Star: "The Digital Architect"
This design system is built for the high-stakes environment of professional 2D animation. The Creative North Star is **"The Digital Architect"**—a philosophy that rejects the cluttered, "button-heavy" aesthetic of legacy software in favor of a monolithic, high-precision workspace. 

We move beyond the "template" look by utilizing **intentional tonal layering**. Instead of defining the workspace with rigid lines, we define it through light and mass. The UI should feel like a custom-milled slate instrument: heavy, reliable, and invisible when you are in "flow state." We achieve this through a "No-Line" architecture, where functionality is revealed through subtle shifts in charcoal depth and surgical strikes of "Point Blue" light.

---

## 2. Colors: The Charcoal Spectrum
The palette is a sophisticated range of desaturated blues and deep charcoals, designed to reduce ocular strain during 12-hour production sessions.

### Surface Hierarchy & Nesting
We do not use borders to separate the workspace. We use **Nesting**.
- **Base Layer (`surface` / `#101319`):** The "ground" of the application. Everything sits on this.
- **Structural Layer (`surface-container-low` / `#191c22`):** Used for large docking areas and background panels.
- **Interaction Layer (`surface-container-high` / `#272a30`):** Used for active widgets, buttons, and input fields.
- **The "No-Line" Rule:** 1px solid borders for sectioning are strictly prohibited. You must define boundaries by placing a `surface-container-high` element against a `surface-container-low` background.

### The "Glass & Gradient" Rule
To prevent the dark theme from feeling "flat" or "dead," use **Linear Gradients** for primary actions. 
- **Signature CTA:** A gradient from `primary` (#b3c5ff) to `primary-container` (#608bff) at a 135-degree angle.
- **Glassmorphism:** For floating palettes or context menus, use `surface-container-highest` (#32353b) at 85% opacity with a **16px Backdrop Blur**. This creates a "frosted obsidian" effect that maintains depth without visual noise.

---

## 3. Typography: The Editorial Tech Scale
We use **Inter** for its mathematical clarity. For the animation environment, typography is treated as data.

- **Display & Headlines:** Use `display-sm` for project titles. Large, low-contrast (`on-surface-variant`) type creates an editorial feel in empty states.
- **The Data Scale:** For timeline numbers, keyframe properties, and coordinate inputs, use **Inter Condensed** (or set `letter-spacing: -0.02em`).
- **Functionality over Flair:** `label-sm` (#0.6875rem) is your workhorse. In a dense animation editor, legibility at small scales is the priority. Use `secondary` (#bec7dc) for labels to keep the focus on the primary values in `primary-fixed` (#dbe1ff).

---

## 4. Elevation & Depth: Tonal Layering
Traditional shadows have no place in a precision technical tool. We use **Tonal Lift**.

- **The Layering Principle:** 
    1. **Canvas:** `surface-container-lowest`
    2. **Panels:** `surface-container-low`
    3. **Widgets:** `surface-container-high`
    4. **Active States:** `surface-container-highest`
- **Ambient Shadows:** Only used for "floating" elements like color pickers or right-click menus. Use a 24px blur, 0px offset, and 8% opacity of the `surface-tint`.
- **The "Ghost Border":** If accessibility requires a stroke (e.g., a focused text input), use the `outline-variant` (#434654) at **20% opacity**. It should feel like a faint reflection on an edge, not a drawn line.

---

## 5. Components: Engineered for Animation

### Buttons & Inputs
- **Primary Action:** Gradient-filled (Primary to Primary-Container) with `sm` (2px) roundedness. No border.
- **Ghost Inputs:** Text fields should have no background; only a `surface-container-highest` bottom-border (2px) that illuminates to `primary` on focus.
- **Sliders:** The "track" is `surface-container-lowest`. The "handle" is a 2px vertical bar of `primary-container`. Avoid "blob" handles; stay architectural.

### Animation Timeline & Tree Views
- **The Playhead:** Use `tertiary-container` (#ff5450) for the playhead line. It is the only element allowed to break the blue/charcoal harmony, signifying its status as the "active moment."
- **Nodal/Tree Views:** Use vertical indentation of 12px instead of "branch lines." Use background-tinting on hover (`surface-variant`) to indicate row selection.
- **Complex Data Tables:** Forbid dividers. Use a zebra-stripe pattern where even rows are `surface-container-low` and odd rows are `surface-container-lowest`.

### Specialized Widgets: Keyframe Handles
- **Diamond Precision:** Keyframes are 6px diamonds.
- **State Logic:** Unselected: `outline`. Selected: `primary`. Active/Modified: `tertiary`.

---

## 6. Do's and Don'ts

### Do
- **Do** use `body-sm` for almost all UI labels to maximize screen real estate.
- **Do** use `surface-bright` (#363940) for hover states on dark buttons to create a "glow" effect.
- **Do** treat "Empty Space" as a functional separator. 16px of gap is better than a 1px line.

### Don't
- **Don't** use pure black (#000000). It kills the "Blue-ish Charcoal" depth of the system.
- **Don't** use 100% opaque borders. They create "visual cages" that distract the animator.
- **Don't** use standard "Rounded" corners. Stick to the `sm` (0.125rem) or `DEFAULT` (0.25rem) scale to keep the editor feeling like a professional tool rather than a consumer app.