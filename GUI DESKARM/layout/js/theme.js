export class ThemeManager {
    constructor() {
        this.container = document.getElementById('theme-buttons-container');
        this.root = document.documentElement;

        // Presets Definition (SWAPPED: Black is now White, White is now Black)
        this.PRESETS = {
            'black': '#ffffff',   // Was #000000, now Pure White
            'white': '#000000',   // Was #ffffff, now Pure Black
            'red': '#b30000',
            'blue': '#0a198f',
            'purple': '#420ca7',
            'green': '#8bc34a',
            'yellow': '#FDD20E'
        };

        // Initialize
        if (this.container) {
            this.createButtons();
            // Default load
            this.applyPreset('black'); // Default to Black as per "ex Default"
        }
    }

    createButtons() {
        this.container.innerHTML = ''; // Clear

        for (const [name, color] of Object.entries(this.PRESETS)) {
            const btn = document.createElement('div');
            btn.className = 'theme-btn';
            btn.title = name.charAt(0).toUpperCase() + name.slice(1);
            btn.style.backgroundColor = color;

            // Add border for black button (which is now visually white) to be visible
            if (name === 'black') {
                btn.style.border = '1px solid #ccc';
            }

            btn.addEventListener('click', () => {
                this.applyPreset(name);
                // Visual feedback
                document.querySelectorAll('.theme-btn').forEach(b => b.classList.remove('active'));
                btn.classList.add('active');
            });

            this.container.appendChild(btn);
        }
    }

    applyPreset(presetName) {
        const baseColor = this.PRESETS[presetName] || this.PRESETS['black'];
        this.updateTheme(baseColor, presetName);
    }

    updateTheme(baseColor, presetName) {
        // Target: Only Sidebar and Robot Plane (Canvas) --bg-secondary

        // 1. Set sidebar/canvas background
        this.root.style.setProperty('--bg-secondary', baseColor);

        // 2. Adjust tertiary (Buttons/Borders) based on this new secondary color
        // If it's very light (White/Yellow), we need a Darker tertiary
        // If it's dark, we need Lighter tertiary

        const isLight = this.isLightColor(baseColor);

        let tertiary;
        if (isLight) {
            // For Light Themes (White, Yellow): Tertiary should be slightly darker to show buttons
            tertiary = this.adjustColor(baseColor, -30);
        } else {
            // For Dark Themes: Tertiary should be slightly lighter
            tertiary = this.adjustColor(baseColor, 20);
        }
        this.root.style.setProperty('--bg-tertiary', tertiary);

        // 3. Smart Contrast (Text Color)
        // "Testo Interfaccia: Diventa BIANCO sul tema Nero/Blu/Viola e NERO sui temi Chiari"
        const textColor = isLight ? '#000000' : '#ffffff';
        this.root.style.setProperty('--text-primary', textColor);
        this.root.style.setProperty('--title-color', textColor); // Section titles match text

        // 4. Axis and Borders
        // After swap: 'black' = white bg, 'white' = black bg
        // Black theme (white bg): axes = BLACK
        // White theme (black bg): axes = WHITE
        // Yellow: WHITE exception
        let axisColor;
        if (presetName === 'black') {
            axisColor = '#000000'; // Black axes on white bg
        } else if (presetName === 'yellow') {
            axisColor = '#ffffff'; // White axes on yellow
        } else {
            axisColor = '#ffffff'; // White axes on dark themes
        }
        this.root.style.setProperty('--axis-color', axisColor);

        // 5. Input Fields
        // "Testo Input: Forzato sempre a Bianco" -> So Background must be Dark.
        // Or "Forzato bianco per contrasto sfondo fisso degli input".
        // Let's force Inputs to be Dark Grey with White Text always.
        this.root.style.setProperty('--input-bg', '#2d2d2d');
        this.root.style.setProperty('--input-text', '#ffffff');

        // 6. Reach Circle Color (User Request)
        // Black theme (white bg): BLACK circle
        // White theme (black bg): WHITE circle
        // Others: WHITE circle
        const reachColor = (presetName === 'black') ? '#000000' : '#ffffff';
        this.root.style.setProperty('--reach-circle-color', reachColor);
    }

    // Helper to detect brightness
    isLightColor(hex) {
        const r = parseInt(hex.substr(1, 2), 16);
        const g = parseInt(hex.substr(3, 2), 16);
        const b = parseInt(hex.substr(5, 2), 16);
        // HSP equation from http://alienryderflex.com/hsp.html
        const hsp = Math.sqrt(
            0.299 * (r * r) +
            0.587 * (g * g) +
            0.114 * (b * b)
        );
        return hsp > 127.5;
    }

    // Helper to lighten/darken hex color
    adjustColor(color, amount) {
        return '#' + color.replace(/^#/, '').replace(/../g, color => ('0' + Math.min(255, Math.max(0, parseInt(color, 16) + amount)).toString(16)).substr(-2));
    }
}
