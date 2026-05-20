#!/usr/bin/env python3
import sys
import re
from pycparser import c_parser, c_ast, c_generator

def strip_comments_preserving_lines(text):
    def replacer(match):
        s = match.group(0)
        res = []
        for char in s:
            if char == '\n':
                res.append('\n')
            else:
                res.append(' ')
        return ''.join(res)

    pattern = re.compile(
        r'//.*?$|/\*.*?\*/',
        re.MULTILINE | re.DOTALL
    )
    return pattern.sub(replacer, text)

def parse_with_auto_typedefs(code_to_parse):
    custom_types = set()
    parser = c_parser.CParser()

    # Pre-populate with standard C types that are commonly used but not keywords
    custom_types.update([
        "uint8_t", "uint16_t", "uint32_t", "uint64_t",
        "int8_t", "int16_t", "int32_t", "int64_t",
        "size_t", "asyncc_task_t", "asyncc_state_t",
        "asyncc_chan_t", "asyncc_gate_t"
    ])

    error_pattern = re.compile(r'(?:([^:]+):)?(\d+):(\d+):\s*(.*)')
    ident_pattern = re.compile(r'\b([a-zA-Z_][a-zA-Z0-9_]*)\b')

    clean_code = strip_comments_preserving_lines(code_to_parse)

    while True:
        typedef_header = "\n".join(f"typedef int {t};" for t in sorted(custom_types))
        full_code = typedef_header + "\n" + clean_code
        header_line_count = typedef_header.count('\n') + (1 if typedef_header else 0)

        try:
            ast = parser.parse(full_code)
            return ast, typedef_header
        except Exception as e:
            err_msg = str(e)
            match = error_pattern.search(err_msg)
            if not match:
                raise e

            raw_line_num = int(match.group(2))
            line_num = raw_line_num - header_line_count
            col_num = int(match.group(3))

            lines = clean_code.splitlines()
            if line_num <= 0 or line_num > len(lines):
                raise e

            err_line = lines[line_num - 1]
            before_err = err_line[:col_num - 1]

            idents = ident_pattern.findall(before_err)
            if not idents:
                raise e

            candidate = idents[-1]
            if candidate in custom_types or candidate in ("struct", "union", "enum", "typedef", "const", "volatile", "restrict"):
                if len(idents) > 1:
                    candidate = idents[-2]
                
            if candidate in custom_types or candidate in ("struct", "union", "enum", "typedef", "const", "volatile", "restrict"):
                raise e

            custom_types.add(candidate)

def transform_ast_node(node, state_names):
    if not isinstance(node, c_ast.Node):
        return

    for slot in node.__slots__:
        val = getattr(node, slot)
        if val is None:
            continue
        elif isinstance(val, list):
            for i, child in enumerate(val):
                if isinstance(child, c_ast.ID):
                    if child.name in state_names:
                        struct_ref = c_ast.StructRef(
                            name=c_ast.ID(name='l'),
                            type='->',
                            field=c_ast.ID(name=child.name),
                            coord=child.coord
                        )
                        val[i] = struct_ref
                else:
                    transform_ast_node(child, state_names)
        elif isinstance(val, c_ast.Node):
            if isinstance(val, c_ast.ID):
                if val.name in state_names:
                    is_struct_field = (isinstance(node, c_ast.StructRef) and slot == 'field')
                    if not is_struct_field:
                        struct_ref = c_ast.StructRef(
                            name=c_ast.ID(name='l'),
                            type='->',
                            field=c_ast.ID(name=val.name),
                            coord=val.coord
                        )
                        setattr(node, slot, struct_ref)
            else:
                transform_ast_node(val, state_names)

def preprocess_code(code):
    # Regex to find async functions:
    # Matches: asyncc <type> <func_name> (<args>) {
    async_func_pattern = re.compile(
        r'asyncc\s+((enum\s+asyncc_state|asyncc_state_t)\s+(\w+)\s*\(([^)]*)\))\s*\{',
        re.MULTILINE
    )

    pos = 0
    while True:
        match = async_func_pattern.search(code, pos)
        if not match:
            break

        func_name = match.group(3)
        args_str = match.group(4)
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

        # Construct a valid C function to parse
        clean_func = f"asyncc_state_t {func_name}({args_str})\n{{{body}}}"

        # Parse the function definition
        try:
            ast, header = parse_with_auto_typedefs(clean_func)
        except Exception as e:
            print(f"Error parsing async function {func_name}: {e}", file=sys.stderr)
            raise e

        # Extract the function body node
        func_def = ast.ext[-1]
        func_body = func_def.body

        # Parse parameters
        func_decl = func_def.decl.type
        params = []
        if func_decl.args is not None:
            for p in func_decl.args.params:
                # If parameter has no name or is void, skip
                if not p.name or (isinstance(p.type, c_ast.TypeDecl) and isinstance(p.type.type, c_ast.IdentifierType) and p.type.type.names == ['void']):
                    continue
                params.append(p)

        param_names = [p.name for p in params]
        generator = c_generator.CGenerator()

        init_args = f"{func_name}_state_t *l"
        if args_str.strip() and args_str.strip() != "void":
            init_args += f", {args_str}"

        init_body = []
        for p in params:
            init_body.append(f"    l->{p.name} = {p.name};")

        # Locate declarations before asyncc_begin
        local_decls = []
        async_begin_idx = -1

        for idx, item in enumerate(func_body.block_items or []):
            if isinstance(item, c_ast.ID) and item.name == 'asyncc_begin':
                async_begin_idx = idx
                break
            elif isinstance(item, c_ast.Decl):
                local_decls.append(item)

        if async_begin_idx == -1:
            # No asyncc_begin found, skip
            pos = end_idx
            continue

        local_names = set(decl.name for decl in local_decls)
        state_names = set(param_names) | local_names
        locals_list = []
        initializers = []

        # Transform local variable initializers
        for decl in local_decls:
            if decl.init is not None:
                transform_ast_node(decl.init, state_names)
                init_str = f"l->{decl.name} = {generator.visit(decl.init)};"
                initializers.append(init_str)
            
            decl.init = None
            decl_str = generator.visit(decl)
            locals_list.append(decl_str)

        # Transform the rest of the body to replace references with l->var
        transform_ast_node(func_body, state_names)

        # Generate struct locals definition
        struct_decl = f"typedef struct {{\n    asyncc_task_t task;\n"
        for p in params:
            struct_decl += f"    {generator.visit(p)};\n"
        for decl_str in locals_list:
            struct_decl += f"    {decl_str};\n"
        struct_decl += f"}} {func_name}_state_t;"

        # Generate init function
        init_decl = f"static inline void {func_name}_init({init_args}) {{\n"
        for init_line in init_body:
            init_decl += f"{init_line}\n"
        init_decl += "}"

        # Format the asyncc_begin call
        begin_replacement = f"    asyncc_begin;"
        if initializers:
            begin_replacement += "\n    " + "\n    ".join(initializers)

        # Format remaining statements using Compound formatting
        remaining_items = func_body.block_items[async_begin_idx + 1:]
        temp_compound = c_ast.Compound(block_items=remaining_items, coord=func_body.coord)
        compound_code = generator.visit(temp_compound)

        # Strip outer braces { and } and keep indentation
        lines = compound_code.splitlines()
        body_content = "\n".join(lines[1:-1])

        transformed_func = (
            f"{struct_decl}\n\n"
            f"{init_decl}\n\n"
            f"asyncc_state_t {func_name}_run(asyncc_task_t *self)\n"
            f"{{\n"
            f"    {func_name}_state_t *l = ({func_name}_state_t*)self;\n"
            f"{begin_replacement}\n"
            f"{body_content}\n"
            f"}}"
        )

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
