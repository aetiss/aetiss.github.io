const githubUsername = 'aetiss';
fetch(`https://api.github.com/users/${githubUsername}`)
  .then(response => response.json())
  .then(data => {
    document.getElementById('avatar').src = data.avatar_url;
    document.getElementById('name').textContent = data.name || data.login;
    document.addEventListener('DOMContentLoaded', function() {
      var avatarElem = document.getElementById('avatar');
      if (avatarElem) {
        avatarElem.src = data.avatar_url;
      }
      var nameElem = document.getElementById('name');
      if (nameElem) {
        nameElem.textContent = data.name || data.login;
      }
      var bioElem = document.getElementById('bio');
      if (bioElem) {
        bioElem.textContent = data.bio || '';
      }
    });
    })
    .catch(err => console.error('GitHub fetch failed', err));

const themeToggle = document.getElementById('theme-toggle');
if (themeToggle) {
  const storedTheme = localStorage.getItem('theme');
  if (storedTheme) {
    document.documentElement.setAttribute('data-theme', storedTheme);
    themeToggle.textContent = storedTheme === 'dark' ? '☀' : '🌙';
  }
  themeToggle.addEventListener('click', () => {
    const currentTheme = document.documentElement.getAttribute('data-theme') === 'dark' ? 'dark' : 'light';
    const newTheme = currentTheme === 'dark' ? 'light' : 'dark';
    document.documentElement.setAttribute('data-theme', newTheme);
    themeToggle.textContent = newTheme === 'dark' ? '☀' : '🌙';
    localStorage.setItem('theme', newTheme);
  });
}
