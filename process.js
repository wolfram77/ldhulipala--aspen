const fs = require('fs');
const os = require('os');
const path = require('path');

const RGRAPH = /^Reading graph \'.*\/(.*?)\.mtx\' \.\.\./m;
const RLOADG = /^Time to load graph: (.+?) ms/m;
const RTRANS = /^Time to transpose graph: (.+?) ms/m;
const RORDER = /^> \|V\|: (\d+), \|E\|: (\d+)/m;
const RBATCH = /^Batch fraction: (.+?) \[(\d+) edges\]/m
const RACQUR = /^Time to acquire version: (.+?) ms/m;
const RDELET = /^Time to delete edges: (.+?) ms/m;
const RINSRT = /^Time to insert edges: (.+?) ms/m;
const RVISIT = /^Time to visit count: (.+?) ms/m;




// *-FILE
// ------

function readFile(pth) {
  var d = fs.readFileSync(pth, 'utf8');
  return d.replace(/\r?\n/g, '\n');
}

function writeFile(pth, d) {
  d = d.replace(/\r?\n/g, os.EOL);
  fs.writeFileSync(pth, d);
}




// *-CSV
// -----

function writeCsv(pth, rows) {
  var cols = Object.keys(rows[0]);
  var a = cols.join()+'\n';
  for (var r of rows)
    a += [...Object.values(r)].map(v => `"${v}"`).join()+'\n';
  writeFile(pth, a);
}




// *-LOG
// -----

function readLogLine(ln, data, state) {
  ln = ln.replace(/^\d+-\d+-\d+ \d+:\d+:\d+\s+/, '');
  if (RGRAPH.test(ln)) {
    var [, graph] = RGRAPH.exec(ln);
    if (!data.has(graph)) data.set(graph, []);
    state.graph = graph;
    state.order = 0;
    state.size  = 0;
    state.batch_fraction = 0;
    state.batch_size     = 0;
    state.time           = 0;
    state.technique      = '';
  }
  else if (RORDER.test(ln)) {
    var [, order, size] = RORDER.exec(ln);
    state.order = parseFloat(order);
    state.size  = parseFloat(size);
  }
  else if (RBATCH.test(ln)) {
    var [, batch_fraction, batch_size] = RBATCH.exec(ln);
    state.batch_fraction   = parseFloat(batch_fraction);
    state.batch_size       = parseFloat(batch_size);
  }
  else if (RLOADG.test(ln)) {
    var [, time] = RLOADG.exec(ln);
    data.get(state.graph).push(Object.assign({}, state, {
      time: parseFloat(time),
      technique: 'loadGraph'
    }));
  }
  else if (RTRANS.test(ln)) {
    var [, time] = RTRANS.exec(ln);
    data.get(state.graph).push(Object.assign({}, state, {
      time: parseFloat(time),
      technique: 'transposeGraph'
    }));
  }
  else if (RACQUR.test(ln)) {
    var [, time] = RACQUR.exec(ln);
    data.get(state.graph).push(Object.assign({}, state, {
      time: parseFloat(time),
      technique: 'acquireVersion'
    }));
  }
  else if (RDELET.test(ln)) {
    var [, time] = RDELET.exec(ln);
    data.get(state.graph).push(Object.assign({}, state, {
      time: parseFloat(time),
      technique: 'deleteEdges'
    }));
  }
  else if (RINSRT.test(ln)) {
    var [, time] = RINSRT.exec(ln);
    data.get(state.graph).push(Object.assign({}, state, {
      time: parseFloat(time),
      technique: 'insertEdges'
    }));
  }
  else if (RVISIT.test(ln)) {
    var last = data.get(state.graph).slice(-1)[0];
    var technique = last.technique==='deleteEdges'? 'visitCount-' : 'visitCount';
    var [, time]  = RVISIT.exec(ln);
    data.get(state.graph).push(Object.assign({}, state, {
      time: parseFloat(time),
      technique,
    }));
  }
  return state;
}

function readLog(pth) {
  var text  = readFile(pth);
  var lines = text.split('\n');
  var data  = new Map();
  var state = {};
  for (var ln of lines)
    state = readLogLine(ln, data, state);
  return data;
}




// PROCESS-*
// ---------

function processCsv(data) {
  var a = [];
  for (var rows of data.values())
    a.push(...rows);
  return a;
}




// MAIN
// ----

function main(cmd, log, out) {
  var data = readLog(log);
  if (path.extname(out)==='') cmd += '-dir';
  switch (cmd) {
    case 'csv':
      var rows = processCsv(data);
      writeCsv(out, rows);
      break;
    case 'csv-dir':
      for (var [graph, rows] of data)
        writeCsv(path.join(out, graph+'.csv'), rows);
      break;
    default:
      console.error(`error: "${cmd}"?`);
      break;
  }
}
main(...process.argv.slice(2));
