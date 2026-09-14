/* M-P4 fixture：worker 自愿 close（应静默，不触发 onerror）。 */
postMessage('ready');
close();
