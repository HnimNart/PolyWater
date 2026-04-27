#!/usr/bin/env python3
"""Add separator banners around function definitions in .slang shader files.

Banner style:
    /**********************************************************/
    void myFunc(int a, int b)
    /**********************************************************/
    {
"""

import re
import sys

BANNER = "/**********************************************************/\n"


def scan_line(line: str, in_block: bool):
    """Scan one line character-by-character, respecting block/line comments and strings.

    Args:
        line:     raw source line (including newline)
        in_block: True if we are already inside a /* ... */ block comment

    Returns:
        (net_braces, net_parens, in_block_after, visible_text)
        where visible_text is the portion of the line outside any comment or string.
    """
    net_b = 0
    net_p = 0
    visible = []
    pos = 0
    while pos < len(line):
        if in_block:
            if line[pos:pos + 2] == '*/':
                in_block = False
                pos += 2
            else:
                pos += 1
        else:
            if line[pos:pos + 2] == '/*':
                in_block = True
                pos += 2
            elif line[pos:pos + 2] == '//':
                break  # rest of line is a line comment
            elif line[pos] == '"':
                # Skip string literal
                pos += 1
                while pos < len(line) and line[pos] != '"':
                    if line[pos] == '\\':
                        pos += 1
                    pos += 1
                if pos < len(line):
                    pos += 1
            elif line[pos] == '{':
                net_b += 1
                visible.append('{')
                pos += 1
            elif line[pos] == '}':
                net_b -= 1
                visible.append('}')
                pos += 1
            elif line[pos] == '(':
                net_p += 1
                visible.append('(')
                pos += 1
            elif line[pos] == ')':
                net_p -= 1
                visible.append(')')
                pos += 1
            else:
                visible.append(line[pos])
                pos += 1
    return net_b, net_p, in_block, ''.join(visible)


def get_indent(line: str) -> str:
    return line[:len(line) - len(line.lstrip())]


def is_attr_or_header_modifier(line: str, in_block: bool) -> bool:
    """True if the line is entirely attribute(s) + optional return-type modifier.

    Handles cases like:
      [shader("compute")]
      [numthreads(64, 1, 1)]  // with trailing comment
      [outputtopology("triangle")] void
      __generic<T>
    """
    if in_block:
        return False
    s = line.strip()
    idx = s.find('//')
    if idx >= 0:
        s = s[:idx].rstrip()
    if not s:
        return False
    if re.match(r'^(?:\[[^\]]*\]\s*)+(?:inline\s+|static\s+|void\s*)?$', s):
        return True
    if re.match(r'^__generic\s*<[^>]*>\s*$', s):
        return True
    return False


def is_comment_or_blank(line: str, in_block: bool) -> bool:
    """True if this line is a comment or blank (considering block-comment state)."""
    if in_block:
        return True
    s = line.strip()
    return not s or s.startswith('//') or s.startswith('*') or s.startswith('/*')


def might_be_func_sig_start(line: str, in_block: bool) -> bool:
    """True if this line could be the start of a function/method signature."""
    if in_block:
        return False
    s = line.strip()
    if not s or s.startswith('#'):
        return False
    if is_comment_or_blank(s, False):
        return False
    if re.match(r'^(struct|interface|enum|namespace|using|typedef)\s', s):
        return False
    if re.match(r'^(layout|groupshared)\s*[\(\[]', s):
        return False
    if '(' not in s:
        return False
    m = re.search(r'\b(\w+)\s*(?:<[^>]*>\s*)?\(', s)
    if not m:
        return False
    word = m.group(1)
    non_func_keywords = {
        'if', 'for', 'while', 'switch', 'do', 'return',
        'layout', 'groupshared', 'defined', 'static_assert',
        'sizeof', 'alignof', 'offsetof',
    }
    if word in non_func_keywords:
        return False
    return True


def update_context(line: str, brace_depth: int, ctx_stack: list, in_block: bool):
    """Update brace_depth and ctx_stack based on brace changes in line.

    Returns (new_brace_depth, in_block_after).
    """
    nb, _, in_block_after, _ = scan_line(line, in_block)
    if nb > 0:
        m = re.match(r'^\s*(struct|interface)\s+', line)
        ctx_stack.append(m.group(1) if m else 'other')
    elif nb < 0:
        for _ in range(-nb):
            if ctx_stack:
                ctx_stack.pop()
    return brace_depth + nb, in_block_after


def emit_func_def(result: list, lines: list,
                  header_idxs: list, sig_idxs: list, brace_idx,
                  indent: str, in_block_at_last_sig: bool):
    """Emit a function definition with banners.

    header_idxs:          indices of attr/modifier/comment lines before the sig
    sig_idxs:             indices of signature lines (return type + name + params)
    brace_idx:            index of line containing '{', or None if '{' is in last sig
    indent:               indentation string for banners
    in_block_at_last_sig: block-comment state just before the last sig line
    """
    banner = indent + BANNER

    last_sig_line = lines[sig_idxs[-1]]
    _, _, _, visible = scan_line(last_sig_line, in_block_at_last_sig)
    brace_in_sig = '{' in visible

    result.append(banner)

    if brace_in_sig:
        brace_pos = visible.rfind('{')

        for idx in header_idxs:
            result.append(lines[idx])

        for idx in sig_idxs[:-1]:
            result.append(lines[idx])

        # Emit last sig line up to (not including) '{'
        # Use the raw line but find the position of the '{' in visible text
        # Map visible brace_pos back to raw line position
        raw_brace_pos = _find_visible_brace_in_raw(last_sig_line, in_block_at_last_sig)
        before_brace = last_sig_line[:raw_brace_pos].rstrip()
        after_brace = visible[brace_pos + 1:].strip()

        result.append(before_brace + '\n')
        result.append(banner)

        if after_brace and after_brace not in ('}', '};'):
            body = after_brace
            has_close = body.endswith('}')
            if has_close:
                body = body[:-1].strip()
            result.append(indent + '{\n')
            result.append(indent + '  ' + body + '\n')
            if has_close:
                result.append(indent + '}\n')
        elif after_brace in ('}', '};'):
            suffix = ';\n' if after_brace == '};' else '\n'
            result.append(indent + '{\n')
            result.append(indent + '}' + suffix)
        else:
            result.append(indent + '{\n')
    else:
        for idx in header_idxs:
            result.append(lines[idx])
        for idx in sig_idxs:
            result.append(lines[idx])
        result.append(banner)
        if brace_idx is not None:
            result.append(lines[brace_idx])


def _find_visible_brace_in_raw(line: str, in_block: bool) -> int:
    """Return the raw index of the last '{' that is outside comments/strings."""
    last = -1
    pos = 0
    while pos < len(line):
        if in_block:
            if line[pos:pos + 2] == '*/':
                in_block = False
                pos += 2
            else:
                pos += 1
        else:
            if line[pos:pos + 2] == '/*':
                in_block = True
                pos += 2
            elif line[pos:pos + 2] == '//':
                break
            elif line[pos] == '"':
                pos += 1
                while pos < len(line) and line[pos] != '"':
                    if line[pos] == '\\':
                        pos += 1
                    pos += 1
                if pos < len(line):
                    pos += 1
            elif line[pos] == '{':
                last = pos
                pos += 1
            else:
                pos += 1
    return last


def process_file(content: str) -> str:
    content = content.replace('\r\n', '\n').replace('\r', '\n')
    lines = content.splitlines(keepends=True)

    # Precompute per-line block-comment state (before the line starts)
    block_before = [False] * len(lines)
    in_b = False
    for idx, ln in enumerate(lines):
        block_before[idx] = in_b
        _, _, in_b, _ = scan_line(ln, in_b)

    result = []
    i = 0
    brace_depth = 0
    ctx_stack = []   # 'struct', 'interface', 'other' per brace depth level
    in_block = False  # current block-comment state

    while i < len(lines):
        line = lines[i]
        ib = block_before[i]  # block state before this line

        in_struct = (brace_depth == 1 and ctx_stack and ctx_stack[-1] == 'struct')
        at_func_level = (brace_depth == 0) or in_struct

        if not at_func_level:
            brace_depth, in_block = update_context(line, brace_depth, ctx_stack, in_block)
            result.append(line)
            i += 1
            continue

        # Step 1: Collect attribute/modifier/comment header lines
        j = i
        header_idxs = []
        while j < len(lines):
            ib_j = block_before[j]
            sj = lines[j].strip()
            if is_attr_or_header_modifier(sj, ib_j):
                header_idxs.append(j)
                j += 1
            elif is_comment_or_blank(sj, ib_j) and header_idxs:
                header_idxs.append(j)
                j += 1
            else:
                break

        # Step 2: Check if lines[j] looks like a function signature start
        if j < len(lines) and might_be_func_sig_start(lines[j].strip(), block_before[j]):
            sig_start = j
            paren_bal = 0
            k = j
            while k < len(lines):
                _, np, _, _ = scan_line(lines[k], block_before[k])
                paren_bal += np
                k += 1
                if paren_bal <= 0:
                    break

            sig_idxs = list(range(sig_start, k))

            if paren_bal <= 0 and sig_idxs:
                last_sig_ib = block_before[sig_idxs[-1]]
                _, _, _, last_visible = scan_line(lines[sig_idxs[-1]], last_sig_ib)
                last_visible = last_visible.rstrip()

                # Interface method or forward declaration (ends with ';')
                if last_visible.endswith(';'):
                    for idx in range(i, k):
                        brace_depth, in_block = update_context(
                            lines[idx], brace_depth, ctx_stack, block_before[idx])
                    result.extend(lines[i:k])
                    i = k
                    continue

                # Step 4: Find the opening '{'
                brace_in_sig = '{' in last_visible

                brace_line_idx = None
                if not brace_in_sig:
                    m = k
                    while m < len(lines) and not lines[m].strip():
                        m += 1
                    if m < len(lines):
                        _, _, _, vis_m = scan_line(lines[m], block_before[m])
                        if '{' in vis_m:
                            brace_line_idx = m

                if brace_in_sig or brace_line_idx is not None:
                    indent = get_indent(
                        lines[header_idxs[0]] if header_idxs else lines[sig_idxs[0]])

                    emit_func_def(
                        result=result,
                        lines=lines,
                        header_idxs=header_idxs,
                        sig_idxs=sig_idxs,
                        brace_idx=brace_line_idx,
                        indent=indent,
                        in_block_at_last_sig=last_sig_ib,
                    )

                    end_idx = k if brace_in_sig else (brace_line_idx + 1)
                    for idx in range(i, end_idx):
                        brace_depth, in_block = update_context(
                            lines[idx], brace_depth, ctx_stack, block_before[idx])
                    i = end_idx
                    continue

                # No '{' found - not a function def
                for idx in range(i, k):
                    brace_depth, in_block = update_context(
                        lines[idx], brace_depth, ctx_stack, block_before[idx])
                result.extend(lines[i:k])
                i = k
                continue

        # No function definition detected - emit first line as-is
        brace_depth, in_block = update_context(line, brace_depth, ctx_stack, ib)
        result.append(line)
        i += 1

    return ''.join(result)


def main():
    files = sys.argv[1:]
    if not files:
        print("Usage: add_banners.py <file1> [file2] ...", file=sys.stderr)
        sys.exit(1)

    for filepath in files:
        try:
            with open(filepath, 'r', encoding='utf-8') as f:
                content = f.read()
            new_content = process_file(content)
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(new_content)
            print(f"OK: {filepath}")
        except Exception as e:
            print(f"ERROR: {filepath}: {e}", file=sys.stderr)
            import traceback
            traceback.print_exc()
            sys.exit(1)


if __name__ == '__main__':
    main()
