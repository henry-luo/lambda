(async function () {
  const media = document.getElementById('media');
  const proto = HTMLMediaElement.prototype;
  console.log(typeof proto.play === 'function' && typeof proto.pause === 'function' && typeof proto.load === 'function');
  console.log(proto.play.length === 0 && proto.pause.length === 0 && proto.load.length === 0);
  console.log(media.paused && Number.isNaN(media.duration) && media.readyState === 0);
  media.muted = true;
  media.volume = 0.25;
  const muted = Object.getOwnPropertyDescriptor(proto,'muted').get;
  console.log(muted.call(media) === true && media.volume === 0.25);
  if (!muted.call(media)) throw new Error('Native muting did not change');
  const pending = media.play();
  console.log(pending instanceof Promise && media.paused === false);
  media.pause();
  try { await pending; console.log(false); }
  catch (error) { console.log(error instanceof DOMException && error.name === 'AbortError' && media.paused); }
  let invalid;
  try { invalid = proto.play.call({}); console.log(invalid instanceof Promise); }
  catch (error) { console.log(false); }
  try { await invalid; console.log(false); }
  catch (error) { console.log(error instanceof TypeError); }
  try { media.volume = 2; console.log(false); }
  catch (error) { console.log(error instanceof DOMException && error.name === 'IndexSizeError' && media.volume === 0.25); }
  const duration = Object.getOwnPropertyDescriptor(proto,'duration').get;
  try { duration.call(new Proxy(media,{})); console.log(false); }
  catch (error) { console.log(error instanceof TypeError); }
  media.src = '../media/test_video_audio.mp4';
  media.load();
  await media.play();
  console.log(!media.paused && media.duration > 0 && media.videoWidth > 0 && media.videoHeight > 0);
  await new Promise(resolve => setTimeout(resolve, 100));
  media.pause();
  console.log(media.paused && media.currentTime > 0);
  media.removeAttribute('src');
  media.load();
  const replaced = media.play();
  media.src = '../media/test_video_audio.mp4';
  media.load();
  try { await replaced; console.log(false); }
  catch (error) { console.log(error instanceof DOMException && error.name === 'AbortError'); }
  console.log(media.muted && media.volume === 0.25);
  media.src = 'data:text/plain,not-media';
  media.load();
  try { await media.play(); console.log(false); }
  catch (error) { console.log(error instanceof DOMException && error.name === 'NotSupportedError'); }
  console.log('MEDIA_PLAYBACK_DONE');
})();
