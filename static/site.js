// Slide rail: highlight the slide that fills most of the screen.
(() => {
    const links = new Map([...document.querySelectorAll(".rail a")].map(a => [a.hash.slice(1), a]));
    const observer = new IntersectionObserver(entries => {
        for (const e of entries) {
            if (!e.isIntersecting) continue;
            links.forEach(a => a.classList.remove("active"));
            links.get(e.target.id)?.classList.add("active");
        }
    }, { threshold: 0.55 });
    links.forEach((_, id) => { const s = document.getElementById(id); if (s) observer.observe(s); });

    // Code snippets start collapsed on phones.
    if (window.matchMedia("(max-width: 700px)").matches)
        document.querySelectorAll("details.snippet").forEach(d => d.removeAttribute("open"));
})();

// Shared helper for the demos.
window.h = (tag, cls, text) => {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text != null) n.textContent = text;
    return n;
};
