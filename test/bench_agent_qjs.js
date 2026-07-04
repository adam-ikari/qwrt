/**
 * Agent Scenario Benchmark — QuickJS / qwrt version
 *
 * Same benchmarks as bench_agent.js but without require('perf_hooks')
 * Uses Date.now() instead of performance.now() for portability.
 */

function now_ms() { return Date.now(); }

function bench(name, fn, iterations) {
    const t0 = now_ms();
    for (let i = 0; i < iterations; i++) fn();
    const elapsed = now_ms() - t0;
    const ops_sec = iterations / (elapsed / 1000.0);
    console.log(`  ${name.padEnd(50)}${ops_sec.toFixed(0)} ops/s  (${elapsed.toFixed(1)} ms)`);
    return { name, value: ops_sec, unit: 'ops/s' };
}

const results = [];

// ============================================================================
// Simulated Agent Data Structures
// ============================================================================

const TOOLS = [
    { name: 'read_file', description: 'Read a file from disk', parameters: { type: 'object', properties: { path: { type: 'string', description: 'File path' } }, required: ['path'] } },
    { name: 'write_file', description: 'Write content to a file', parameters: { type: 'object', properties: { path: { type: 'string' }, content: { type: 'string' } }, required: ['path', 'content'] } },
    { name: 'search', description: 'Search for patterns in code', parameters: { type: 'object', properties: { query: { type: 'string' }, max_results: { type: 'number' } }, required: ['query'] } },
    { name: 'shell', description: 'Execute a shell command', parameters: { type: 'object', properties: { command: { type: 'string' }, timeout: { type: 'number' } }, required: ['command'] } },
    { name: 'memory_save', description: 'Save to agent memory', parameters: { type: 'object', properties: { key: { type: 'string' }, value: { type: 'string' } }, required: ['key', 'value'] } },
    { name: 'memory_recall', description: 'Recall from agent memory', parameters: { type: 'object', properties: { query: { type: 'string' }, limit: { type: 'number' } }, required: ['query'] } },
    { name: 'list_files', description: 'List files in directory', parameters: { type: 'object', properties: { path: { type: 'string' }, recursive: { type: 'boolean' } }, required: ['path'] } },
    { name: 'mcp_call', description: 'Call an MCP server tool', parameters: { type: 'object', properties: { server: { type: 'string' }, tool: { type: 'string' }, args: { type: 'object' } }, required: ['server', 'tool'] } },
];

function makeUserMsg(text) { return { role: 'user', content: text }; }
function makeAssistantMsg(text, toolCalls) {
    const msg = { role: 'assistant', content: text || null };
    if (toolCalls) msg.tool_calls = toolCalls;
    return msg;
}
function makeToolResult(id, result) { return { role: 'tool', tool_call_id: id, content: result }; }

function makeLLMToolResponse(toolName, args) {
    const id = 'call_' + Math.random().toString(36).slice(2, 10);
    return {
        tool_calls: [{
            id,
            type: 'function',
            function: { name: toolName, arguments: JSON.stringify(args) }
        }]
    };
}

// ============================================================================
// 1. Message Construction
// ============================================================================

console.log('\n=== 1. Agent Message Construction ===');

results.push(bench('agent.make_messages_10', () => {
    const msgs = [];
    for (let i = 0; i < 5; i++) {
        msgs.push(makeUserMsg('What does function foo do in bar.ts?'));
        msgs.push(makeAssistantMsg('Let me read that file.', makeLLMToolResponse('read_file', { path: '/src/bar.ts' }).tool_calls));
        msgs.push(makeToolResult('call_abc123', 'function foo() { return 42; }'));
        msgs.push(makeAssistantMsg('The function foo returns 42.'));
    }
    return msgs;
}, 5000));

results.push(bench('agent.make_messages_50', () => {
    const msgs = [];
    for (let i = 0; i < 25; i++) {
        msgs.push(makeUserMsg('Search for the definition of class Baz'));
        msgs.push(makeAssistantMsg(null, makeLLMToolResponse('search', { query: 'class Baz', max_results: 10 }).tool_calls));
        msgs.push(makeToolResult('call_def456', JSON.stringify([{file:'baz.ts',line:42,text:'class Baz { }'}])));
    }
    return msgs;
}, 1000));

// ============================================================================
// 2. Tool Call Parsing
// ============================================================================

console.log('\n=== 2. Tool Call Parsing ===');

results.push(bench('agent.parse_tool_call', () => {
    const resp = makeLLMToolResponse('read_file', { path: '/src/main.ts' });
    for (const tc of resp.tool_calls) {
        const tool = TOOLS.find(t => t.name === tc.function.name);
        if (!tool) continue;
        const args = JSON.parse(tc.function.arguments);
        const valid = tool.parameters.required.every(r => r in args);
    }
}, 10000));

results.push(bench('agent.parse_multi_tool_calls', () => {
    const resp = { tool_calls: [
        makeLLMToolResponse('search', { query: 'TODO' }).tool_calls[0],
        makeLLMToolResponse('read_file', { path: '/src/a.ts' }).tool_calls[0],
        makeLLMToolResponse('shell', { command: 'git log --oneline -5' }).tool_calls[0],
    ]};
    const parsed = [];
    for (const tc of resp.tool_calls) {
        const tool = TOOLS.find(t => t.name === tc.function.name);
        if (tool) parsed.push({ tool, args: JSON.parse(tc.function.arguments), id: tc.id });
    }
    return parsed;
}, 10000));

// ============================================================================
// 3. Context Window Management
// ============================================================================

console.log('\n=== 3. Context Window Management ===');

function buildLongConversation(turns) {
    const msgs = [{ role: 'system', content: 'You are a helpful coding assistant.' }];
    for (let i = 0; i < turns; i++) {
        msgs.push(makeUserMsg(`Question ${i}: explain the code at line ${i * 10}`));
        msgs.push(makeAssistantMsg(null, makeLLMToolResponse('read_file', { path: `/src/file${i % 5}.ts` }).tool_calls));
        msgs.push(makeToolResult(`call_${i}`, `// content of file${i % 5}.ts line ${i * 10}\nfunction handler() { return ${i}; }`));
        msgs.push(makeAssistantMsg(`Line ${i * 10} defines a handler function returning ${i}.`));
    }
    return msgs;
}

const longConv = buildLongConversation(30);

results.push(bench('agent.trim_context_121msgs', () => {
    const MAX_TOKENS = 8000;
    const msgs = longConv.slice();
    let totalEst = msgs.reduce((s, m) => s + JSON.stringify(m).length / 4, 0);
    while (totalEst > MAX_TOKENS && msgs.length > 4) {
        const removed = msgs.splice(1, 1);
        totalEst -= JSON.stringify(removed[0]).length / 4;
    }
    return msgs.length;
}, 5000));

results.push(bench('agent.tokenize_estimate_121msgs', () => {
    let total = 0;
    for (const m of longConv) {
        total += JSON.stringify(m).length / 4;
    }
    return total;
}, 5000));

// ============================================================================
// 4. Memory Operations
// ============================================================================

console.log('\n=== 4. Agent Memory ===');

const memStore = new Map();

results.push(bench('agent.memory_save_100', () => {
    for (let i = 0; i < 100; i++) {
        const key = `memory/${Date.now()}/${i}`;
        const entry = { key, value: `Fact ${i}: The handler at line ${i*10} returns ${i}`, ts: Date.now(), tags: ['code', `file${i%5}`] };
        memStore.set(key, entry);
    }
}, 100));

results.push(bench('agent.memory_recall_scan_100', () => {
    const query = 'handler';
    const results = [];
    for (const [k, v] of memStore) {
        if (v.value.includes(query) || v.tags.some(t => t.includes(query))) {
            results.push(v);
        }
    }
    return results.length;
}, 500));

results.push(bench('agent.memory_json_roundtrip_100', () => {
    const entries = [];
    for (let i = 0; i < 100; i++) {
        const entry = { key: `k${i}`, value: `v${i}`, ts: Date.now() };
        entries.push(JSON.parse(JSON.stringify(entry)));
    }
    return entries.length;
}, 200));

// ============================================================================
// 5. Full Agent Loop (single turn, no I/O)
// ============================================================================

console.log('\n=== 5. Full Agent Loop (single turn, no I/O) ===');

results.push(bench('agent.full_turn_no_io', () => {
    const msgs = [{ role: 'system', content: 'You are a helpful assistant.' }];
    for (let i = 0; i < 5; i++) {
        msgs.push(makeUserMsg(`Question ${i}`));
        msgs.push(makeAssistantMsg(`Answer ${i}`));
    }
    msgs.push(makeUserMsg('What does foo do?'));

    const body = {
        model: 'gpt-4',
        messages: msgs,
        tools: TOOLS.map(t => ({ type: 'function', function: { name: t.name, description: t.description, parameters: t.parameters } })),
        temperature: 0.7,
        max_tokens: 4096,
    };
    const bodyJson = JSON.stringify(body);

    const llmResp = makeLLMToolResponse('read_file', { path: '/src/foo.ts' });
    const parsedCalls = [];
    for (const tc of llmResp.tool_calls) {
        const tool = TOOLS.find(t => t.name === tc.function.name);
        if (tool) {
            const args = JSON.parse(tc.function.arguments);
            parsedCalls.push({ id: tc.id, tool, args });
        }
    }

    const toolResults = parsedCalls.map(pc => ({
        tool_call_id: pc.id,
        content: JSON.stringify({ ok: true, data: 'function foo() { return 42; }' })
    }));

    msgs.push(makeAssistantMsg(null, llmResp.tool_calls));
    for (const tr of toolResults) {
        msgs.push(makeToolResult(tr.tool_call_id, tr.content));
    }

    return msgs.length;
}, 5000));

results.push(bench('agent.full_turn_with_context_trim', () => {
    const msgs = buildLongConversation(15);
    msgs.push(makeUserMsg('Explain the main handler'));

    let totalEst = msgs.reduce((s, m) => s + JSON.stringify(m).length / 4, 0);
    while (totalEst > 4000 && msgs.length > 4) {
        const removed = msgs.splice(1, 1);
        totalEst -= JSON.stringify(removed[0]).length / 4;
    }

    const resp = makeLLMToolResponse('search', { query: 'handler' });
    const calls = resp.tool_calls.map(tc => ({
        id: tc.id, name: tc.function.name, args: JSON.parse(tc.function.arguments)
    }));

    msgs.push(makeAssistantMsg(null, resp.tool_calls));
    for (const c of calls) {
        msgs.push(makeToolResult(c.id, JSON.stringify({ matches: 3 })));
    }

    return msgs.length;
}, 2000));

// ============================================================================
// 6. Streaming SSE Parsing
// ============================================================================

console.log('\n=== 6. Streaming SSE Parsing ===');

function makeSSEChunks(n) {
    const chunks = [];
    for (let i = 0; i < n; i++) {
        chunks.push(`data: {"id":"chatcmpl-abc","object":"chat.completion.chunk","choices":[{"delta":{"content":"Hello"},"index":0}]}\n\n`);
    }
    chunks.push('data: [DONE]\n\n');
    return chunks;
}

const sseChunks = makeSSEChunks(50);

results.push(bench('agent.parse_sse_50chunks', () => {
    let fullContent = '';
    for (const chunk of sseChunks) {
        const lines = chunk.split('\n');
        for (const line of lines) {
            if (!line.startsWith('data: ') || line === 'data: [DONE]') continue;
            try {
                const data = JSON.parse(line.slice(6));
                if (data.choices && data.choices[0].delta && data.choices[0].delta.content) {
                    fullContent += data.choices[0].delta.content;
                }
            } catch (e) {}
        }
    }
    return fullContent.length;
}, 10000));

results.push(bench('agent.parse_sse_200chunks', () => {
    const chunks200 = makeSSEChunks(200);
    let fullContent = '';
    for (const chunk of chunks200) {
        const lines = chunk.split('\n');
        for (const line of lines) {
            if (!line.startsWith('data: ') || line === 'data: [DONE]') continue;
            try {
                const data = JSON.parse(line.slice(6));
                if (data.choices && data.choices[0].delta && data.choices[0].delta.content) {
                    fullContent += data.choices[0].delta.content;
                }
            } catch (e) {}
        }
    }
    return fullContent.length;
}, 2000));

// ============================================================================
// 7. MCP Protocol
// ============================================================================

console.log('\n=== 7. MCP Protocol ===');

results.push(bench('agent.mcp_build_request', () => {
    const req = {
        jsonrpc: '2.0',
        id: Math.floor(Math.random() * 100000),
        method: 'tools/call',
        params: {
            name: 'read_file',
            arguments: { path: '/src/main.ts' }
        }
    };
    return JSON.stringify(req);
}, 10000));

results.push(bench('agent.mcp_parse_response', () => {
    const resp = '{"jsonrpc":"2.0","id":42,"result":{"content":[{"type":"text","text":"function main() { return 1; }"}]}}';
    const parsed = JSON.parse(resp);
    const text = parsed.result.content[0].text;
    return text.length;
}, 10000));

results.push(bench('agent.mcp_discover_tools', () => {
    const tools = [];
    for (let i = 0; i < 20; i++) {
        tools.push({
            name: `tool_${i}`,
            description: `Tool number ${i} for doing thing ${i}`,
            inputSchema: {
                type: 'object',
                properties: { arg1: { type: 'string' }, arg2: { type: 'number' } },
                required: ['arg1']
            }
        });
    }
    const resp = JSON.stringify({ jsonrpc: '2.0', id: 1, result: { tools } });
    const parsed = JSON.parse(resp);
    const registry = new Map();
    for (const t of parsed.result.tools) {
        registry.set(t.name, t);
    }
    return registry.size;
}, 5000));

// ============================================================================
console.log('\n=== JSON Results ===');
console.log(JSON.stringify(results, null, 2));
