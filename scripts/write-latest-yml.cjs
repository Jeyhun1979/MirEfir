const fs = require('fs')
const path = require('path')
const crypto = require('crypto')

const exe = process.argv[2]
const version = process.argv[3]
if (!exe || !version || !fs.existsSync(exe)) {
  console.error('Usage: node scripts/write-latest-yml.cjs <setup.exe> <version>')
  process.exit(1)
}

const buf = fs.readFileSync(exe)
const sha512 = crypto.createHash('sha512').update(buf).digest('base64')
const name = path.basename(exe)
const yaml = [
  `version: ${version}`,
  'files:',
  `  - url: ${name}`,
  `    sha512: ${sha512}`,
  `    size: ${buf.length}`,
  `path: ${name}`,
  `sha512: ${sha512}`,
  `releaseDate: '${new Date().toISOString()}'`,
  '',
].join('\n')

const out = path.join(path.dirname(exe), 'latest.yml')
fs.writeFileSync(out, yaml)
console.log(`Wrote ${out}`)
