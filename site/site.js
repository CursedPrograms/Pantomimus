// Pantomimus' page: the feature cards and the year
(function () {
  'use strict';
  const features = [
    ['Any webcam', 'Full-body tracking from the camera you have, or from a video file: 33 landmarks in real-world metres, at three quality levels.'],
    ['Smooth and steady', 'One Euro smoothing with every setting a slider, confidence handling, and feet that stay on the floor.'],
    ['A real skeleton', 'Landmarks solved into joint rotations on a 53-bone game-engine skeleton, with Mixamo naming as an option and root motion.'],
    ['Takes', 'Record, trim, scrub, resample, smooth and loop your takes, and save them in a project.'],
    ['Export', 'BVH, FBX and glTF with the skeleton, for Blender, Prometheus, Demiurge, Unity or Unreal.'],
    ['Face capture', 'ARKit\'s 52 blendshapes, the irises and the head\'s turn from your face, as Live Link Face CSV or live onto shape keys in Blender.'],
    ['Sharpie markers', 'Draw about 25 dots where Pantomimus shows you on your own face, calibrate once, and they sharpen the brows, cheeks, lips and jaw.'],
    ['Live link', 'Move a character as you move: the Blender add-on drives any armature by bone names and records keyframes, and Pygmalion poses its person live.']
  ];
  const grid = document.getElementById('features');
  if (grid) {
    for (const [title, text] of features) {
      const card = document.createElement('article');
      const h = document.createElement('h2');
      h.textContent = title;
      const p = document.createElement('p');
      p.textContent = text;
      card.append(h, p);
      grid.appendChild(card);
    }
  }
  const year = document.getElementById('year');
  if (year) year.textContent = String(Math.max(2026, new Date().getFullYear()));
})();
