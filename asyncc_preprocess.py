#!/usr/bin/env python3
import sys
import re

def preprocess_code(code):
    # Regex to find async functions:
    # Matches: async enum async <func_name> (<args>) {
    # Captures: function name, arguments list, and body
    async_func_pattern = re.compile(
        r'async\s+(enum\s+async\s+(\w+)\s*\(([^)]*)\))\s*\{',
        re.MULTILINE
    )

    pos = 0
    while True:
        match = async_func_pattern.search(code, pos)
        if not match:
            break

        full_decl = match.group(1) # "enum async func_name(args)"
        func_name = match.group(2)
        args_str = match.group(3)
        start_idx = match.end()

        # Find the matching closing brace for the function body
        brace_count = 1
        end_idx = start_idx
        while brace_count > 0 and end_idx < len(code):
            if code[end_idx] == '{':
                brace_count += 1
            elif code[end_idx] == '}':
                brace_count -= 1
            end_idx += 1

        if brace_count > 0:
            # Unbalanced braces, skip
            pos = start_idx
            continue

        body = code[start_idx:end_idx - 1]

        # Extract the stack variable name from arguments
        # Typically the first argument is "uint8_t *s" or similar
        stack_match = re.search(r'uint8_t\s*\*\s*([a-zA-Z_][a-zA-Z0-9_]*)', args_str)
        stack_name = stack_match.group(1) if stack_match else 's'

        # Parse local variable declarations before "async_begin"
        # Matches lines like: type name; or type name = init;
        var_decl_pattern = re.compile(
            r'^\s*([a-zA-Z_][a-zA-Z0-9_]*\s*(?:\*\s*)*)\s*([a-zA-Z_][a-zA-Z0-9_]*(?:\s*\[\s*[a-zA-Z0-9_]+\s*\])*)(?:\s*=\s*([^;]+))?\s*;',
            re.MULTILINE
        )

        # We search up to "async_begin;" in the body
        begin_match = re.search(r'\basync_begin\s*;', body)
        if not begin_match:
            pos = end_idx
            continue

        pre_begin_zone = body[:begin_match.start()]
        post_begin_zone = body[begin_match.end():]

        locals_list = []
        initializers = []
        
        # Extract declarations
        for decl_match in var_decl_pattern.finditer(pre_begin_zone):
            var_type = decl_match.group(1).strip()
            var_name_raw = decl_match.group(2).strip()
            initializer = decl_match.group(3)

            # Separate array brackets if any
            array_match = re.match(r'([a-zA-Z_][a-zA-Z0-9_]*)\s*(\[.*\])', var_name_raw)
            if array_match:
                var_name = array_match.group(1)
                full_decl_in_struct = f"{var_type} {var_name}{array_match.group(2)}"
            else:
                var_name = var_name_raw
                full_decl_in_struct = f"{var_type} {var_name}"

            locals_list.append(full_decl_in_struct)
            
            if initializer:
                initializers.append(f"l->{var_name} = {initializer.strip()};")

        # Replace references to these local variables in post_begin_zone
        # We must avoid matching:
        # - member accesses (e.g. obj.var or obj->var)
        # - other words containing the var name
        new_post_begin = post_begin_zone
        for decl_in_struct in locals_list:
            # Extract just the variable name
            var_name = re.search(r'([a-zA-Z_][a-zA-Z0-9_]*)(?:\[|;|\s*$)', decl_in_struct).group(1)
            
            # Pattern: not preceded by "." or "->"
            var_ref_pattern = re.compile(rf'(?<!\.)(?<!->)\b{var_name}\b')
            new_post_begin = var_ref_pattern.sub(f"l->{var_name}", new_post_begin)

        # Format the async_begin call
        locals_args = ", ".join(locals_list)
        begin_replacement = f"async_begin({stack_name}, {locals_args});"
        if initializers:
            begin_replacement += "\n    " + "\n    ".join(initializers)

        # Replace async_end; with async_end(stack_name);
        new_post_begin = re.sub(r'\basync_end\s*;', f"async_end({stack_name});", new_post_begin)

        # Construct the transformed function body
        transformed_body = pre_begin_zone
        # Strip declarations from the pre_begin zone
        transformed_body = var_decl_pattern.sub('', transformed_body)
        transformed_body += begin_replacement + new_post_begin

        # Re-assemble the function
        transformed_func = f"{full_decl}\n{{{transformed_body}}}"
        
        # Replace in original code
        code = code[:match.start()] + transformed_func + code[end_idx:]
        
        # Update pos for next search
        pos = match.start() + len(transformed_func)

    return code

def main():
    if len(sys.argv) < 3:
        print("Usage: asyncc_preprocess.py <input_file> <output_file>")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    with open(input_file, 'r') as f:
        content = f.read()

    processed_content = preprocess_code(content)

    with open(output_file, 'w') as f:
        f.write(processed_content)

if __name__ == '__main__':
    main()
