// The fixture pages' "bundler", run as their `npm run build` with vite's
// --outDir: every file under the page's src/, its path and its text, into one
// index.html, so CheckPage.cmake can read exactly what the build saw.
import { mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from 'node:fs';
import { join, relative } from 'node:path';

const args = process.argv.slice(2);
const at = args.indexOf('--outDir');
const outDir = at >= 0 ? args[at + 1] : 'dist';

function filesUnder(dir)
{
    return readdirSync(dir).sort().flatMap((name) =>
    {
        const path = join(dir, name);
        return statSync(path).isDirectory() ? filesUnder(path) : [path];
    });
}

const sections = filesUnder('src').map((path) =>
    `=== ${relative('src', path).split('\\').join('/')}\n${readFileSync(path, 'utf8')}`);

rmSync(outDir, { recursive: true, force: true });
mkdirSync(outDir, { recursive: true });
writeFileSync(join(outDir, 'index.html'), sections.join('\n'));
