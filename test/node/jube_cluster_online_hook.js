const cluster = require('cluster');
const script = typeof __filename === 'string' ? __filename : 'test/node/jube_cluster_online_hook.js';

if (cluster.isPrimary) {
  const worker = cluster.fork();
  worker.on('online', () => {
    console.log('primary online');
    worker.kill();
  });
  worker.on('exit', () => console.log('primary exit'));
} else {
  console.log('worker started');
}
