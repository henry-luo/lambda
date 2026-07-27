const cluster = require('cluster');
const nodeCluster = require('node:cluster');

console.log(cluster === nodeCluster, cluster.default === cluster);
console.log(cluster.isPrimary, cluster.isMaster, cluster.isWorker);
console.log(typeof cluster.setupPrimary, typeof cluster.setupMaster, typeof cluster.fork);
