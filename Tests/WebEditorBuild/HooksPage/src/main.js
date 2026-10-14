// The hooks fixture's own page, beside what the build copies and writes in.
import { common } from './shared/common.js';
import { looks } from './shared/looks.js';
import { info } from './build/info.js';

export const page = [common, looks, info];
