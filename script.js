fetch('https://api.github.com/users/aetiss')
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