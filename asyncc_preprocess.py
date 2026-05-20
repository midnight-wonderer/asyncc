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
        "size_t"
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

def transform_ast_node(node, local_names, stack_name):
    if not isinstance(node, c_ast.Node):
        return

    for slot in node.__slots__:
        val = getattr(node, slot)
        if val is None:
            continue
        elif isinstance(val, list):
            for i, child in enumerate(val):
                if isinstance(child, c_ast.ID):
                    if child.name in local_names:
                        struct_ref = c_ast.StructRef(
                            name=c_ast.ID(name='l'),
                            type='->',
                            field=c_ast.ID(name=child.name),
                            coord=child.coord
                        )
                        val[i] = struct_ref
                    elif child.name == 'asyncc_end':
                        func_call = c_ast.FuncCall(
                            name=c_ast.ID(name='asyncc_end', coord=child.coord),
                            args=c_ast.ExprList(exprs=[c_ast.ID(name=stack_name, coord=child.coord)], coord=child.coord),
                            coord=child.coord
                        )
                        val[i] = func_call
                else:
                    transform_ast_node(child, local_names, stack_name)
        elif isinstance(val, c_ast.Node):
            if isinstance(val, c_ast.ID):
                if val.name in local_names:
                    is_struct_field = (isinstance(node, c_ast.StructRef) and slot == 'field')
                    if not is_struct_field:
                        struct_ref = c_ast.StructRef(
                            name=c_ast.ID(name='l'),
                            type='->',
                            field=c_ast.ID(name=val.name),
                            coord=val.coord
                        )
                        setattr(node, slot, struct_ref)
                elif val.name == 'asyncc_end':
                    func_call = c_ast.FuncCall(
                        name=c_ast.ID(name='asyncc_end', coord=val.coord),
                        args=c_ast.ExprList(exprs=[c_ast.ID(name=stack_name, coord=val.coord)], coord=val.coord),
                        coord=val.coord
                    )
                    setattr(node, slot, func_call)
            else:
                transform_ast_node(val, local_names, stack_name)

def preprocess_code(code):
    # Regex to find async functions:
    # Matches: asyncc enum asyncc_state <func_name> (<args>) {
    # Captures: function name, arguments list, and body
    async_func_pattern = re.compile(
        r'asyncc\s+(enum\s+asyncc_state\s+(\w+)\s*\(([^)]*)\))\s*\{',
        re.MULTILINE
    )

    pos = 0
    while True:
        match = async_func_pattern.search(code, pos)
        if not match:
            break

        full_decl = match.group(1) # "enum asyncc_state func_name(args)"
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

        # Construct a valid C function to parse
        clean_func = f"{full_decl}\n{{{body}}}"

        # Parse the function definition
        try:
            ast, header = parse_with_auto_typedefs(clean_func)
        except Exception as e:
            print(f"Error parsing async function {func_name}: {e}", file=sys.stderr)
            raise e

        # Extract the function body node
        func_def = ast.ext[-1]
        func_body = func_def.body

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
        locals_list = []
        initializers = []

        generator = c_generator.CGenerator()

        # Transform local variable initializers
        for decl in local_decls:
            if decl.init is not None:
                transform_ast_node(decl.init, local_names, stack_name)
                init_str = f"l->{decl.name} = {generator.visit(decl.init)};"
                initializers.append(init_str)
            
            decl.init = None
            decl_str = generator.visit(decl)
            locals_list.append(decl_str)

        # Transform the rest of the body to replace references with l->var
        transform_ast_node(func_body, local_names, stack_name)

        # Format the asyncc_begin call
        locals_args = ", ".join(locals_list)
        begin_replacement = f"    asyncc_begin({stack_name}, {locals_args});"
        if initializers:
            begin_replacement += "\n    " + "\n    ".join(initializers)

        # Format remaining statements using Compound formatting
        remaining_items = func_body.block_items[async_begin_idx + 1:]
        temp_compound = c_ast.Compound(block_items=remaining_items, coord=func_body.coord)
        compound_code = generator.visit(temp_compound)

        # Strip outer braces { and } and keep indentation
        lines = compound_code.splitlines()
        body_content = "\n".join(lines[1:-1])

        transformed_func = f"{full_decl}\n{{\n{begin_replacement}\n{body_content}\n}}"

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
