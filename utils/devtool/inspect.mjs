#!/usr/bin/env node
import path from 'node:path';
import fs from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { InspectorTransport } from './inspector-transport.cjs';
import { InspectorClient } from './src/inspector/client.mjs';
import { decodeMark, encodeMark } from './src/inspector/mark.mjs';

const projectRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const [document, requestFile] = process.argv.slice(2);
if (!document || document === '--help') {
  console.log('Usage: node utils/devtool/inspect.mjs <document> [request.mark]');
  console.log('Without a request file, reads the root DOM branch. The target runs headlessly.');
  process.exit(document ? 0 : 1);
}
const transport = new InspectorTransport(projectRoot, document);
transport.on('output', text => process.stderr.write(text));
const client = new InspectorClient(transport);
try {
  await client.request('hello', { version: 1 });
  const request = requestFile ? decodeMark(await fs.readFile(requestFile, 'utf8')) : { op: { name: 'tree' } };
  const { id, op, ...fields } = request;
  if (!op?.name) throw new Error('request file must contain an op symbol');
  const result = await client.request(op.name, fields);
  console.log(encodeMark(result));
} catch (error) {
  console.error(`devtools: ${error.message}`);
  process.exitCode = 1;
} finally {
  client.disconnect();
  transport.close();
}
