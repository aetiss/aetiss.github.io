fetch('https://api.github.com/users/aetiss')
  .then(response => response.json())
  .then(data => {
    document.getElementById('avatar').src = data.avatar_url;
    document.getElementById('name').textContent = data.name || data.login;
    document.getElementById('bio').textContent = data.bio || '';
  })
  .catch(err => console.error('GitHub fetch failed', err));
