const username = "aetiss";

async function loadProfile() {
  try {
    const res = await fetch(`https://api.github.com/users/${username}`);
    if (!res.ok) throw new Error("GitHub request failed");
    const data = await res.json();
    document.getElementById("avatar").src = data.avatar_url;
    document.getElementById("name").textContent = data.name || data.login;
    document.getElementById("bio").textContent = data.bio || "";
    document.getElementById("github-link").href = data.html_url;
  } catch (err) {
    console.error(err);
  }
}

async function loadRepos() {
  try {
    const res = await fetch(
      `https://api.github.com/users/${username}/repos?sort=updated&per_page=6`,
    );
    if (!res.ok) throw new Error("Repo request failed");
    const repos = await res.json();
    const list = document.getElementById("project-list");
    repos.forEach((repo) => {
      const li = document.createElement("li");
      li.innerHTML = `<a href="${repo.html_url}" target="_blank" rel="noopener">${repo.name}</a>`;
      list.appendChild(li);
    });
  } catch (err) {
    console.error(err);
  }
}

function setYear() {
  document.getElementById("year").textContent = new Date().getFullYear();
}

loadProfile();
loadRepos();
setYear();
