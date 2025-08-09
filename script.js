(async () => {
  const username = "aetiss";
  try {
    const res = await fetch(`https://api.github.com/users/${username}`);
    const data = await res.json();
    const avatar = document.getElementById("avatar");
    const nameEl = document.getElementById("name");
    const bioEl = document.getElementById("bio");
    if (avatar) avatar.src = data.avatar_url;
    if (nameEl) nameEl.textContent = data.name || data.login;
    if (bioEl) bioEl.textContent = data.bio || "";
  } catch (err) {
    console.error("GitHub fetch failed", err);
  }
})();

const toggle = document.getElementById("theme-toggle");
if (toggle) {
  const prefersDark = window.matchMedia("(prefers-color-scheme: dark)").matches;
  const stored = localStorage.getItem("theme");
  const theme = stored || (prefersDark ? "dark" : "light");
  document.documentElement.setAttribute("data-theme", theme);
  toggle.textContent = theme === "dark" ? "☀️" : "🌙";
  toggle.addEventListener("click", () => {
    const current = document.documentElement.getAttribute("data-theme");
    const next = current === "dark" ? "light" : "dark";
    document.documentElement.setAttribute("data-theme", next);
    toggle.textContent = next === "dark" ? "☀️" : "🌙";
    localStorage.setItem("theme", next);
  });
}
