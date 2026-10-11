/* Small C-like compiler. Emits position-independent i386 code and an ELF32 image.
   No host libc, assembler, linker, or interpreter is needed in the guest. */
#include "compiler.h"
#include "sys_api.h"
#include <stddef.h>

#define TOKENS 16384u
#define FUNCTIONS 128u
#define GLOBALS 128u
#define LOCALS 256u
#define ARGUMENTS 8u
#define FIXUPS 8192u
#define DATA_BYTES 65536u
#define ENTRY 96u
#define NONE 0xffffffffu
enum {
    T_EOF = 256, T_ID, T_NUMBER, T_STRING, T_INT, T_CHAR, T_VOID, T_IF, T_ELSE,
    T_WHILE, T_RETURN, T_BREAK, T_CONTINUE, T_SIZEOF, T_EQ, T_NE, T_LE, T_GE,
    T_AND, T_OR, T_SHL, T_SHR, T_INC, T_DEC, T_ADD_SET, T_SUB_SET, T_MUL_SET,
    T_DIV_SET, T_MOD_SET
};
typedef struct { uint32_t kind, value, start, length, line, column; } token_t;
typedef struct {
    uint32_t name, type, argc, argtypes[ARGUMENTS], params[ARGUMENTS], body, end, address;
    uint32_t thread_address;
    int defined, referenced;
} function_t;
typedef struct { uint32_t name, type, count, address, scope; } variable_t;
typedef struct { uint32_t at, target; } fixup_t;
typedef struct { uint32_t start, breaks; } loop_t;
typedef struct { uint32_t type; int lvalue, null_constant; } expression_t;
typedef struct {
    const uint8_t *source; uint8_t *image; szc_result_t *result;
    uint32_t size, capacity, ntokens, pos, pc, nfunctions, nglobals, nlocals;
    uint32_t data_size, ndatafix, ncallfix, nfunctionfix, local_bytes, scope, depth, nloops;
    uint32_t return_type, returns;
    token_t tokens[TOKENS];
    function_t functions[FUNCTIONS];
    variable_t globals[GLOBALS], locals[LOCALS];
    fixup_t datafix[FIXUPS], callfix[FIXUPS], functionfix[FIXUPS];
    loop_t loops[32];
    uint8_t data[DATA_BYTES];
} compiler_t;
typedef struct { const char *name; uint32_t id, type, argc, args[5]; } builtin_t;
static const builtin_t builtins[] = {
#define SZ_SERVICE(id, name, result, argc, a, b, c, d, e) {#name, id, result, argc, {a,b,c,d,e}},
#include "sys_api.def"
#undef SZ_SERVICE
};
static void zero(void *p, uint32_t n) { uint8_t *out = p; while (n--) *out++ = 0; }
static void copy(void *p, const void *s, uint32_t n) {
    uint8_t *out = p; const uint8_t *in = s; while (n--) *out++ = *in++;
}
static void diagnostic(compiler_t *c, const char *message, uint32_t line, uint32_t column) {
    if (!c->result->error) {
        c->result->error = message; c->result->line = line; c->result->column = column;
    }
}
static token_t *token(compiler_t *c) { return &c->tokens[c->pos]; }
static void fail(compiler_t *c, const char *message) {
    diagnostic(c, message, token(c)->line, token(c)->column);
}
static int accept(compiler_t *c, uint32_t kind) {
    if (c->result->error || token(c)->kind != kind) return 0;
    if (kind != T_EOF) c->pos++;
    return 1;
}
static void expect(compiler_t *c, uint32_t kind, const char *message) {
    if (!accept(c, kind)) fail(c, message);
}
static int named(compiler_t *c, uint32_t index, const char *name) {
    token_t *t = &c->tokens[index]; uint32_t i = 0;
    while (name[i] && i < t->length && name[i] == c->source[t->start + i]) i++;
    return i == t->length && !name[i];
}
static int same_name(compiler_t *c, uint32_t a, uint32_t b) {
    token_t *x = &c->tokens[a], *y = &c->tokens[b];
    if (x->length != y->length) return 0;
    for (uint32_t i = 0; i < x->length; i++)
        if (c->source[x->start + i] != c->source[y->start + i]) return 0;
    return 1;
}
static int alpha(uint8_t ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'; }
static int digit(uint8_t ch) { return ch >= '0' && ch <= '9'; }
static int hex(uint8_t ch) {
    if (digit(ch)) return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
static uint8_t advance(compiler_t *c, uint32_t *at, uint32_t *line, uint32_t *column) {
    uint8_t ch = c->source[(*at)++];
    if (ch == '\n') { (*line)++; *column = 1; } else (*column)++;
    return ch;
}
static uint8_t escaped(compiler_t *c, uint32_t *at, uint32_t *line, uint32_t *column) {
    if (*at >= c->size) { diagnostic(c, "Incomplete escape", *line, *column); return 0; }
    uint8_t ch = advance(c, at, line, column);
    switch (ch) {
    case 'n': return '\n'; case 'r': return '\r'; case 't': return '\t'; case '0': return 0;
    case '\\': case '\'': case '"': return ch;
    case 'x': {
        uint32_t value = 0, n = 0;
        while (n < 2 && *at < c->size && hex(c->source[*at]) >= 0) {
            value = value * 16 + hex(advance(c, at, line, column)); n++;
        }
        if (!n) diagnostic(c, "Expected hexadecimal escape digits", *line, *column);
        return value;
    }
    default: diagnostic(c, "Unsupported escape", *line, *column); return 0;
    }
}
static void data_byte(compiler_t *c, uint8_t value) {
    if (c->data_size == DATA_BYTES) { fail(c, "String/global data limit exceeded"); return; }
    c->data[c->data_size++] = value;
}
static void lex(compiler_t *c) {
    uint32_t at = 0, line = 1, column = 1;
    while (at < c->size && !c->result->error) {
        uint8_t ch = c->source[at];
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') { advance(c, &at, &line, &column); continue; }
        if (ch == '/' && at + 1 < c->size && c->source[at + 1] == '/') {
            while (at < c->size && c->source[at] != '\n') advance(c, &at, &line, &column);
            continue;
        }
        if (ch == '/' && at + 1 < c->size && c->source[at + 1] == '*') {
            uint32_t first_line = line, first_column = column;
            advance(c, &at, &line, &column); advance(c, &at, &line, &column);
            while (at + 1 < c->size && !(c->source[at] == '*' && c->source[at + 1] == '/'))
                advance(c, &at, &line, &column);
            if (at + 1 >= c->size) { diagnostic(c, "Unterminated comment", first_line, first_column); break; }
            advance(c, &at, &line, &column); advance(c, &at, &line, &column); continue;
        }
        if (c->ntokens == TOKENS - 1) { diagnostic(c, "Token limit exceeded", line, column); break; }
        token_t *t = &c->tokens[c->ntokens++];
        *t = (token_t){.kind = ch, .start = at, .line = line, .column = column};
        if (alpha(ch)) {
            do { advance(c, &at, &line, &column); } while (at < c->size && (alpha(c->source[at]) || digit(c->source[at])));
            t->kind = T_ID; t->length = at - t->start;
            if (t->length > 63) { diagnostic(c, "Identifier exceeds 63 characters", t->line, t->column); break; }
            static const struct { const char *name; uint32_t kind; } keywords[] = {
                {"int", T_INT}, {"char", T_CHAR}, {"void", T_VOID}, {"if", T_IF}, {"else", T_ELSE},
                {"while", T_WHILE}, {"return", T_RETURN}, {"break", T_BREAK},
                {"continue", T_CONTINUE}, {"sizeof", T_SIZEOF}
            };
            for (uint32_t i = 0; i < sizeof keywords / sizeof *keywords; i++)
                if (named(c, c->ntokens - 1, keywords[i].name)) t->kind = keywords[i].kind;
        } else if (digit(ch)) {
            uint32_t base = 10, value = 0, n = 0;
            if (ch == '0' && at + 1 < c->size && (c->source[at + 1] == 'x' || c->source[at + 1] == 'X')) {
                base = 16; advance(c, &at, &line, &column); advance(c, &at, &line, &column);
            }
            while (at < c->size && (base == 16 ? hex(c->source[at]) >= 0 : digit(c->source[at]))) {
                uint32_t d = hex(advance(c, &at, &line, &column));
                if (value > (0xffffffffu - d) / base) { diagnostic(c, "Integer literal exceeds 32 bits", t->line, t->column); break; }
                value = value * base + d; n++;
            }
            if (!n) diagnostic(c, "Expected digits after 0x", t->line, t->column);
            if (at < c->size && alpha(c->source[at])) diagnostic(c, "Invalid integer literal", line, column);
            t->kind = T_NUMBER; t->value = value;
        } else if (ch == '"' || ch == '\'') {
            advance(c, &at, &line, &column); uint32_t first = c->data_size, n = 0, value = 0;
            while (at < c->size && c->source[at] != ch && !c->result->error) {
                if (c->source[at] == '\n' || !c->source[at]) { diagnostic(c, "Newline/NUL in literal", line, column); break; }
                uint8_t byte = advance(c, &at, &line, &column);
                if (byte == '\\') byte = escaped(c, &at, &line, &column);
                if (ch == '"') data_byte(c, byte); else value = byte;
                n++;
            }
            if (at >= c->size || c->source[at] != ch) { diagnostic(c, "Unterminated literal", t->line, t->column); break; }
            advance(c, &at, &line, &column);
            if (ch == '\'') {
                if (n != 1) diagnostic(c, "Character literal must contain one byte", t->line, t->column);
                t->kind = T_NUMBER; t->value = value;
            } else { data_byte(c, 0); t->kind = T_STRING; t->value = first; t->length = n; }
        } else {
            advance(c, &at, &line, &column);
            uint8_t next = at < c->size ? c->source[at] : 0;
            static const struct { uint8_t a, b; uint32_t kind; } pairs[] = {
                {'=','=',T_EQ}, {'!','=',T_NE}, {'<','=',T_LE}, {'>','=',T_GE},
                {'&','&',T_AND}, {'|','|',T_OR}, {'<','<',T_SHL}, {'>','>',T_SHR},
                {'+','+',T_INC}, {'-','-',T_DEC}, {'+','=',T_ADD_SET}, {'-','=',T_SUB_SET},
                {'*','=',T_MUL_SET}, {'/','=',T_DIV_SET}, {'%','=',T_MOD_SET}
            };
            for (uint32_t i = 0; i < sizeof pairs / sizeof *pairs; i++)
                if (ch == pairs[i].a && next == pairs[i].b) { t->kind = pairs[i].kind; advance(c, &at, &line, &column); break; }
            const char *allowed = "(){}[];,+-*/%&|^~!=<>";
            int valid = 0; while (*allowed) if (*allowed++ == ch) valid = 1;
            if (!valid || !ch) diagnostic(c, "Unsupported character (no preprocessor directives)", t->line, t->column);
        }
    }
    c->tokens[c->ntokens++] = (token_t){.kind = T_EOF, .line = line, .column = column};
}
static int is_type(uint32_t kind) { return kind == T_INT || kind == T_CHAR || kind == T_VOID; }
static int pointer(uint32_t type) { return type >= SZ_POINTER; }
static uint32_t type_size(uint32_t type) { return pointer(type) || type == SZ_INT ? 4 : type == SZ_CHAR ? 1 : 0; }
static uint32_t base_type(compiler_t *c) {
    uint32_t kind = token(c)->kind;
    if (!is_type(kind)) { fail(c, "Expected int, char, or void"); return SZ_INT; }
    c->pos++; return kind == T_INT ? SZ_INT : kind == T_CHAR ? SZ_CHAR : SZ_VOID;
}
static uint32_t stars(compiler_t *c, uint32_t type) {
    while (accept(c, '*')) {
        type += SZ_POINTER;
        if (type > 4 * SZ_POINTER + SZ_VOID) fail(c, "Pointer nesting limit exceeded");
    }
    return type;
}
static int compatible(uint32_t left, expression_t right) {
    if (left == SZ_VOID || right.type == SZ_VOID) return 0;
    if (!pointer(left) && !pointer(right.type)) return 1;
    if (pointer(left) && right.null_constant) return 1;
    return pointer(left) && pointer(right.type) &&
           (left == right.type || left == SZ_VOID_PTR || right.type == SZ_VOID_PTR);
}
static int builtin_index(compiler_t *c, uint32_t name) {
    for (uint32_t i = 0; i < sizeof builtins / sizeof *builtins; i++) if (named(c, name, builtins[i].name)) return i;
    return -1;
}
static int function_index(compiler_t *c, uint32_t name) {
    for (uint32_t i = 0; i < c->nfunctions; i++) if (same_name(c, name, c->functions[i].name)) return i;
    return -1;
}
static variable_t *variable(compiler_t *c, uint32_t name) {
    for (uint32_t i = c->nlocals; i; i--) if (same_name(c, name, c->locals[i - 1].name)) return &c->locals[i - 1];
    for (uint32_t i = 0; i < c->nglobals; i++) if (same_name(c, name, c->globals[i].name)) return &c->globals[i];
    return 0;
}
static uint32_t name_token(compiler_t *c) {
    uint32_t name = c->pos; expect(c, T_ID, "Expected an identifier"); return name;
}
static uint32_t array_count(compiler_t *c, int allow_empty) {
    if (!accept(c, '[')) return 0;
    uint32_t count = token(c)->value;
    if (allow_empty && token(c)->kind == ']') count = NONE;
    else {
        expect(c, T_NUMBER, "Array length must be an integer literal");
        if (!count || count > 16384) fail(c, "Array length must be 1..16384");
    }
    expect(c, ']', "Expected ]"); return count;
}
static void put32(uint8_t *p, uint32_t value) { for (uint32_t i = 0; i < 4; i++) p[i] = value >> (8 * i); }
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void global_declaration(compiler_t *c, uint32_t base, uint32_t type, uint32_t name) {
    for (;;) {
        if (c->nglobals == GLOBALS) { fail(c, "Global variable limit exceeded"); return; }
        if (variable(c, name) || function_index(c, name) >= 0 || builtin_index(c, name) >= 0) fail(c, "Duplicate/reserved global name");
        uint32_t count = array_count(c, 1), value = 0, string = NONE, string_size = 0;
        if (type == SZ_VOID) fail(c, "A variable cannot have void type");
        if (accept(c, '=')) {
            if (count && type == SZ_CHAR && token(c)->kind == T_STRING) {
                string = token(c)->value; string_size = token(c)->length + 1; c->pos++;
                if (count == NONE) count = string_size;
                if (string_size > count) fail(c, "String does not fit in array");
            } else {
                int negative = accept(c, '-'); if (!negative) accept(c, '+');
                value = token(c)->value; expect(c, T_NUMBER, "Global initializer must be a constant number or char-array string");
                if (negative) value = 0u - value;
                if (count || (pointer(type) && value)) fail(c, "Unsupported global initializer");
            }
        }
        if (count == NONE) { fail(c, "Unsized array requires a string initializer"); count = 1; }
        if (c->result->error) return;
        uint32_t bytes = type_size(type) * (count ? count : 1);
        while (c->data_size % 4) data_byte(c, 0);
        uint32_t address = c->data_size;
        if (bytes > DATA_BYTES - address) { fail(c, "Global data limit exceeded"); return; }
        zero(c->data + address, bytes); c->data_size += bytes;
        if (string != NONE) copy(c->data + address, c->data + string, string_size);
        else if (!count) { if (type == SZ_CHAR) c->data[address] = value; else put32(c->data + address, value); }
        c->globals[c->nglobals++] = (variable_t){name, type, count, address, 0};
        if (!accept(c, ',')) break;
        type = stars(c, base); name = name_token(c);
    }
    expect(c, ';', "Expected ; after global declaration");
}
static void declarations(compiler_t *c) {
    while (token(c)->kind != T_EOF && !c->result->error) {
        uint32_t base = base_type(c), type = stars(c, base), name = name_token(c);
        if (!accept(c, '(')) { global_declaration(c, base, type, name); continue; }
        function_t f = {.name = name, .type = type};
        if (builtin_index(c, name) >= 0 || variable(c, name)) fail(c, "Function name is reserved or already a variable");
        if (token(c)->kind == T_VOID && c->tokens[c->pos + 1].kind == ')') c->pos++;
        else if (token(c)->kind != ')') do {
            if (f.argc == ARGUMENTS) { fail(c, "Functions support at most eight parameters"); break; }
            uint32_t arg = stars(c, base_type(c));
            if (arg == SZ_VOID) fail(c, "Parameter cannot have void type");
            f.argtypes[f.argc] = arg;
            f.params[f.argc] = token(c)->kind == T_ID ? c->pos++ : NONE;
            f.argc++;
        } while (accept(c, ','));
        expect(c, ')', "Expected ) after parameters");
        if (accept(c, ';')) f.defined = 0;
        else {
            expect(c, '{', "Expected function body or prototype");
            f.defined = 1; f.body = c->pos - 1; uint32_t level = 1;
            for (uint32_t i = 0; i < f.argc; i++) if (f.params[i] == NONE) fail(c, "Function definition requires parameter names");
            while (level && token(c)->kind != T_EOF && !c->result->error) {
                uint32_t kind = token(c)->kind; c->pos++;
                if (kind == '{') level++;
                if (kind == '}') level--;
            }
            if (level) fail(c, "Unterminated function body");
            f.end = c->pos;
        }
        int previous = function_index(c, name);
        if (previous >= 0) {
            function_t *old = &c->functions[previous];
            if (old->type != f.type || old->argc != f.argc) fail(c, "Conflicting function declarations");
            for (uint32_t i = 0; i < f.argc; i++) if (old->argtypes[i] != f.argtypes[i]) fail(c, "Conflicting parameter types");
            if (f.defined) { if (old->defined) fail(c, "Function already defined"); else *old = f; }
        } else if (c->nfunctions == FUNCTIONS) fail(c, "Function limit exceeded");
        else c->functions[c->nfunctions++] = f;
    }
}
static void byte(compiler_t *c, uint8_t value) {
    if (c->result->error) return;
    if (c->pc >= c->capacity) { fail(c, "Executable exceeds output capacity"); return; }
    c->image[c->pc++] = value;
}
static void word(compiler_t *c, uint32_t value) { for (uint32_t i = 0; i < 4; i++) byte(c, value >> (8 * i)); }
static void op2(compiler_t *c, uint8_t a, uint8_t b) { byte(c, a); byte(c, b); }
static void immediate(compiler_t *c, uint32_t value) { byte(c, 0xb8); word(c, value); }
static void patch(compiler_t *c, uint32_t at, uint32_t value) {
    if (c->result->error) return;
    if (at > c->pc || c->pc - at < 4) { fail(c, "Internal patch error"); return; }
    put32(c->image + at, value);
}
static void target(compiler_t *c, uint32_t at, uint32_t address) { patch(c, at, address - at - 4); }
static uint32_t jump(compiler_t *c, uint8_t condition) {
    if (condition) { byte(c, 0x0f); byte(c, condition); } else byte(c, 0xe9);
    uint32_t at = c->pc; word(c, 0); return at;
}
static uint32_t chain_jump(compiler_t *c, uint32_t previous) {
    uint32_t at = jump(c, 0); patch(c, at, previous); return at;
}
static void patch_chain(compiler_t *c, uint32_t at, uint32_t address) {
    while (at != NONE && !c->result->error) {
        if (at > c->pc || c->pc - at < 4) { fail(c, "Internal jump-chain error"); return; }
        uint32_t next = get32(c->image + at); target(c, at, address); at = next;
    }
}
static void data_address(compiler_t *c, uint32_t address) {
    op2(c, 0x8d, 0x83); uint32_t at = c->pc; word(c, 0);
    if (c->ndatafix == FIXUPS) fail(c, "Data-reference limit exceeded");
    else c->datafix[c->ndatafix++] = (fixup_t){at, address};
}
static void local_address(compiler_t *c, uint32_t address) { op2(c, 0x8d, 0x85); word(c, address); }
static void value(compiler_t *c, expression_t *e) {
    if (e->type == SZ_VOID) { fail(c, "Void expression has no value"); return; }
    if (e->lvalue) {
        if (e->type == SZ_CHAR) { op2(c, 0x0f, 0xb6); byte(c, 0x00); }
        else op2(c, 0x8b, 0x00);
        e->lvalue = 0;
    }
}
static void boolean(compiler_t *c, uint8_t condition) {
    op2(c, 0x0f, condition); byte(c, 0xc0); op2(c, 0x0f, 0xb6); byte(c, 0xc0);
}
static void truncate_char(compiler_t *c, uint32_t type) {
    if (type == SZ_CHAR) { byte(c, 0x25); word(c, 255); }
}
static void store_ecx(compiler_t *c, uint32_t type) {
    truncate_char(c, type); op2(c, type == SZ_CHAR ? 0x88 : 0x89, 0x01);
}
static void call(compiler_t *c, uint32_t argc, int builtin, uint32_t index) {
    /* Expressions pushed arguments left-to-right. Copy into a correctly ordered,
       16-byte-aligned cdecl call frame; EBP restores the evaluation stack. */
    op2(c, 0x89, 0xe1); byte(c, 0x55); op2(c, 0x89, 0xe5);
    op2(c, 0x83, 0xe4); byte(c, 0xf0);
    uint32_t physical = builtin ? 6 : argc, bytes = (physical * 4 + 15) & ~15u;
    op2(c, 0x81, 0xec); word(c, bytes);
    if (builtin) {
        op2(c, 0x89, 0x34); byte(c, 0x24); /* hidden environment argument */
        op2(c, 0x31, 0xc0);
        for (uint32_t i = 1; i < 6; i++) { op2(c, 0x89, 0x44); byte(c, 0x24); byte(c, i * 4); }
    }
    for (uint32_t i = 0; i < argc; i++) {
        op2(c, 0x8b, 0x81); word(c, (argc - 1 - i) * 4);
        op2(c, 0x89, 0x84); byte(c, 0x24); word(c, (i + !!builtin) * 4);
    }
    if (builtin) { op2(c, 0xff, 0x97); word(c, SZ_API_FUNCTIONS_OFFSET + index * 4); }
    else {
        byte(c, 0xe8); uint32_t at = c->pc; word(c, 0);
        if (c->ncallfix == FIXUPS) fail(c, "Function-call limit exceeded");
        else c->callfix[c->ncallfix++] = (fixup_t){at, index};
    }
    op2(c, 0x89, 0xec); byte(c, 0x5d);
    op2(c, 0x81, 0xc4); word(c, argc * 4);
}

/* Parsing and statement generation follow below. EAX carries a value or an
   lvalue address; EBX is the image base, ESI the environment, EDI the API. */
static expression_t expression(compiler_t *c, int minimum);
static expression_t unary(compiler_t *c);
static int integer(uint32_t type) { return type == SZ_INT || type == SZ_CHAR; }
static expression_t result_value(uint32_t type) { return (expression_t){type, 0, 0}; }
static void binary(compiler_t *c, uint32_t operator, uint32_t left, expression_t right) {
    /* Left value is ECX, right value is EAX. */
    int lp = pointer(left), rp = pointer(right.type);
    if (operator == '+' || operator == '-') {
        if (lp && integer(right.type)) {
            uint32_t unit = type_size(left - SZ_POINTER);
            if (!unit) { fail(c, "Cannot perform arithmetic on void pointers"); return; }
            if (unit == 4) { op2(c, 0xc1, 0xe0); byte(c, 2); }
        } else if (operator == '+' && integer(left) && rp) {
            uint32_t unit = type_size(right.type - SZ_POINTER);
            if (!unit) { fail(c, "Cannot perform arithmetic on void pointers"); return; }
            if (unit == 4) { op2(c, 0xc1, 0xe1); byte(c, 2); }
        } else if (operator == '-' && lp && rp && left == right.type) {
            uint32_t unit = type_size(left - SZ_POINTER);
            if (!unit) { fail(c, "Cannot subtract void pointers"); return; }
            op2(c, 0x29, 0xc1); op2(c, 0x89, 0xc8);
            if (unit == 4) { op2(c, 0xc1, 0xf8); byte(c, 2); }
            return;
        } else if (!integer(left) || !integer(right.type)) { fail(c, "Invalid pointer arithmetic"); return; }
        if (operator == '+') op2(c, 0x01, 0xc8);
        else { op2(c, 0x29, 0xc1); op2(c, 0x89, 0xc8); }
        return;
    }
    if (operator == T_EQ || operator == T_NE || operator == '<' || operator == '>' || operator == T_LE || operator == T_GE) {
        if (lp || rp) {
            if (!compatible(left, right) && !(rp && integer(left))) { fail(c, "Incompatible pointer comparison"); return; }
        } else if (!integer(left) || !integer(right.type)) { fail(c, "Invalid comparison"); return; }
        uint8_t cc = operator == T_EQ ? 0x94 : operator == T_NE ? 0x95 :
                     operator == '<' ? 0x9c : operator == '>' ? 0x9f : operator == T_LE ? 0x9e : 0x9d;
        if (lp || rp) {
            if (operator == '<') cc = 0x92;
            if (operator == '>') cc = 0x97;
            if (operator == T_LE) cc = 0x96;
            if (operator == T_GE) cc = 0x93;
        }
        op2(c, 0x39, 0xc1); boolean(c, cc); return;
    }
    if (!integer(left) || !integer(right.type)) { fail(c, "Operator requires integer operands"); return; }
    switch (operator) {
    case '*': op2(c, 0x0f, 0xaf); byte(c, 0xc1); break;
    case '/': case '%':
        byte(c, 0x91); byte(c, 0x99); op2(c, 0xf7, 0xf9);
        if (operator == '%') op2(c, 0x89, 0xd0);
        break;
    case '&': op2(c, 0x21, 0xc8); break;
    case '|': op2(c, 0x09, 0xc8); break;
    case '^': op2(c, 0x31, 0xc8); break;
    case T_SHL: case T_SHR:
        byte(c, 0x91); op2(c, 0xd3, operator == T_SHL ? 0xe0 : 0xf8); break;
    default: fail(c, "Unsupported operator"); break;
    }
}
static expression_t increment(compiler_t *c, expression_t e, int decrease, int postfix) {
    if (!e.lvalue) { fail(c, "Increment requires a writable scalar"); return result_value(SZ_INT); }
    uint32_t unit = pointer(e.type) ? type_size(e.type - SZ_POINTER) : 1;
    if (!unit) fail(c, "Cannot increment a void pointer");
    byte(c, 0x50); value(c, &e); op2(c, 0x89, 0xc2); /* preserve old value in EDX */
    byte(c, decrease ? 0x2d : 0x05); word(c, unit);
    byte(c, 0x59); store_ecx(c, e.type);
    if (postfix) op2(c, 0x89, 0xd0);
    return result_value(e.type);
}
static expression_t primary(compiler_t *c) {
    expression_t e = result_value(SZ_INT); token_t t = *token(c);
    if (accept(c, T_NUMBER)) { immediate(c, t.value); e.null_constant = !t.value; }
    else if (accept(c, T_STRING)) { data_address(c, t.value); e.type = SZ_CHAR_PTR; }
    else if (accept(c, T_ID)) {
        uint32_t name = c->pos - 1;
        if (accept(c, '(')) {
            int bi = builtin_index(c, name), fi = function_index(c, name);
            if (bi < 0 && fi < 0) { fail(c, "Unknown function"); return e; }
            const builtin_t *b = bi >= 0 ? &builtins[bi] : 0;
            function_t *f = fi >= 0 ? &c->functions[fi] : 0;
            uint32_t expected = b ? b->argc : f->argc, argc = 0;
            if (token(c)->kind != ')') do {
                if (argc >= expected) { fail(c, "Too many function arguments"); break; }
                expression_t arg = expression(c, 1); value(c, &arg);
                if (!compatible(b ? b->args[argc] : f->argtypes[argc], arg)) fail(c, "Incompatible argument type");
                byte(c, 0x50); argc++;
            } while (accept(c, ','));
            expect(c, ')', "Expected ) after arguments");
            if (argc != expected) fail(c, "Wrong number of function arguments");
            call(c, argc, !!b, b ? b->id : (uint32_t)fi);
            e.type = b ? b->type : f->type;
        } else {
            variable_t *v = variable(c, name);
            if (!v) {
                int fi = function_index(c, name);
                if (fi < 0) { fail(c, "Unknown variable"); return e; }
                function_t *f = &c->functions[fi];
                if (f->type != SZ_INT || f->argc != 1 || f->argtypes[0] != SZ_INT) {
                    fail(c, "Thread entry must be int name(int argument)"); return e;
                }
                op2(c, 0x8d, 0x83); uint32_t at = c->pc; word(c, 0);
                if (c->nfunctionfix == FIXUPS) fail(c, "Function-reference limit exceeded");
                else c->functionfix[c->nfunctionfix++] = (fixup_t){at, (uint32_t)fi};
                f->referenced = 1; e.type = SZ_VOID_PTR;
            } else {
                if (v->scope) local_address(c, v->address); else data_address(c, v->address);
                e.type = v->type + (v->count ? SZ_POINTER : 0); e.lvalue = !v->count;
            }
        }
    } else if (accept(c, '(')) {
        e = expression(c, 1); expect(c, ')', "Expected )");
    } else { fail(c, "Expected an expression"); return e; }
    while (!c->result->error) {
        if (accept(c, '[')) {
            value(c, &e);
            if (!pointer(e.type) || !type_size(e.type - SZ_POINTER)) fail(c, "Indexing requires a typed pointer or array");
            byte(c, 0x50); expression_t index = expression(c, 1); value(c, &index);
            if (!integer(index.type)) fail(c, "Array index must be an integer");
            expect(c, ']', "Expected ] after index");
            if (type_size(e.type - SZ_POINTER) == 4) { op2(c, 0xc1, 0xe0); byte(c, 2); }
            byte(c, 0x59); op2(c, 0x01, 0xc8);
            e = (expression_t){e.type - SZ_POINTER, 1, 0};
        } else if (accept(c, T_INC)) e = increment(c, e, 0, 1);
        else if (accept(c, T_DEC)) e = increment(c, e, 1, 1);
        else break;
    }
    return e;
}
static expression_t unary(compiler_t *c) {
    if (c->depth >= 64) { fail(c, "Expression nesting limit exceeded"); return result_value(SZ_INT); }
    c->depth++;
    uint32_t kind = token(c)->kind; expression_t e;
    if (kind == T_INC || kind == T_DEC) {
        c->pos++; e = increment(c, unary(c), kind == T_DEC, 0);
    } else if (kind == '+' || kind == '-' || kind == '!' || kind == '~' || kind == '*' || kind == '&') {
        c->pos++; e = unary(c);
        if (kind == '&') {
            if (!e.lvalue) fail(c, "Address-of requires a scalar lvalue");
            e = result_value(e.type + SZ_POINTER);
        } else {
            value(c, &e);
            if (kind == '*') {
                if (!pointer(e.type) || !type_size(e.type - SZ_POINTER)) fail(c, "Dereference requires a typed pointer");
                e = (expression_t){e.type - SZ_POINTER, 1, 0};
            } else {
                if (kind != '!' && !integer(e.type)) fail(c, "Unary arithmetic requires an integer");
                if (kind == '-') op2(c, 0xf7, 0xd8);
                if (kind == '~') op2(c, 0xf7, 0xd0);
                if (kind == '!') { op2(c, 0x85, 0xc0); boolean(c, 0x94); }
                e = result_value(SZ_INT);
            }
        }
    } else if (kind == T_SIZEOF || (kind == '(' && is_type(c->tokens[c->pos + 1].kind))) {
        int size = kind == T_SIZEOF; c->pos++;
        if (size) expect(c, '(', "sizeof supports parenthesized types");
        uint32_t type = stars(c, base_type(c)); expect(c, ')', "Expected ) after type");
        if (size) {
            if (!type_size(type)) fail(c, "sizeof(void) is invalid");
            immediate(c, type_size(type)); e = result_value(SZ_INT);
        } else {
            e = unary(c); value(c, &e); truncate_char(c, type); e = result_value(type);
        }
    } else e = primary(c);
    c->depth--; return e;
}
static int precedence(uint32_t kind) {
    switch (kind) {
    case '=': case T_ADD_SET: case T_SUB_SET: case T_MUL_SET: case T_DIV_SET: case T_MOD_SET: return 1;
    case T_OR: return 2; case T_AND: return 3; case '|': return 4; case '^': return 5; case '&': return 6;
    case T_EQ: case T_NE: return 7; case '<': case '>': case T_LE: case T_GE: return 8;
    case T_SHL: case T_SHR: return 9; case '+': case '-': return 10; case '*': case '/': case '%': return 11;
    default: return 0;
    }
}
static expression_t expression(compiler_t *c, int minimum) {
    if (c->depth >= 64) { fail(c, "Expression nesting limit exceeded"); return result_value(SZ_INT); }
    c->depth++; expression_t left = unary(c);
    while (!c->result->error) {
        uint32_t operator = token(c)->kind; int level = precedence(operator);
        if (level < minimum || !level) break;
        c->pos++;
        if (level == 1) {
            if (!left.lvalue) { fail(c, "Assignment requires a writable scalar"); break; }
            uint32_t type = left.type; byte(c, 0x50);
            if (operator != '=') { value(c, &left); byte(c, 0x50); }
            expression_t right = expression(c, level); value(c, &right);
            if (operator != '=') {
                byte(c, 0x59);
                uint32_t arithmetic = operator == T_ADD_SET ? '+' : operator == T_SUB_SET ? '-' :
                                      operator == T_MUL_SET ? '*' : operator == T_DIV_SET ? '/' : '%';
                binary(c, arithmetic, type, right);
                right = result_value(pointer(type) ? type : SZ_INT);
            }
            if (!compatible(type, right)) fail(c, "Incompatible assignment type");
            byte(c, 0x59); store_ecx(c, type); left = result_value(type);
        } else if (operator == T_AND || operator == T_OR) {
            value(c, &left); op2(c, 0x85, 0xc0);
            uint32_t short_jump = jump(c, operator == T_AND ? 0x84 : 0x85);
            expression_t right = expression(c, level + 1); value(c, &right);
            op2(c, 0x85, 0xc0); boolean(c, 0x95);
            uint32_t end = jump(c, 0); target(c, short_jump, c->pc);
            immediate(c, operator == T_OR); target(c, end, c->pc);
            left = result_value(SZ_INT);
        } else {
            value(c, &left); byte(c, 0x50);
            expression_t right = expression(c, level + 1); value(c, &right);
            if ((operator == T_EQ || operator == T_NE || operator == '<' || operator == '>' ||
                 operator == T_LE || operator == T_GE) &&
                integer(left.type) && pointer(right.type) && !left.null_constant)
                fail(c, "Incompatible pointer comparison");
            byte(c, 0x59); binary(c, operator, left.type, right);
            uint32_t type = SZ_INT;
            if ((operator == '+' || operator == '-') && pointer(left.type) && integer(right.type)) type = left.type;
            if (operator == '+' && integer(left.type) && pointer(right.type)) type = right.type;
            left = result_value(type);
        }
    }
    c->depth--; return left;
}
static void add_local(compiler_t *c, uint32_t name, uint32_t type, uint32_t count, uint32_t address) {
    if (c->nlocals == LOCALS) { fail(c, "Local variable limit exceeded"); return; }
    if (builtin_index(c, name) >= 0 || function_index(c, name) >= 0) fail(c, "Variable name conflicts with a function");
    for (uint32_t i = c->nlocals; i && c->locals[i - 1].scope == c->scope; i--)
        if (same_name(c, name, c->locals[i - 1].name)) fail(c, "Duplicate variable in this scope");
    if (c->result->error) return;
    c->locals[c->nlocals++] = (variable_t){name, type, count, address, c->scope};
}
static void local_declaration(compiler_t *c) {
    uint32_t base = base_type(c);
    do {
        uint32_t type = stars(c, base), name = name_token(c), count = array_count(c, 0);
        if (type == SZ_VOID) fail(c, "A variable cannot have void type");
        uint32_t bytes = type_size(type) * (count ? count : 1);
        if (bytes > 16384 || c->local_bytes > 16384 - bytes) { fail(c, "Function local storage exceeds 16 KiB"); return; }
        c->local_bytes = (c->local_bytes + bytes + 3) & ~3u;
        uint32_t address = 0u - c->local_bytes; add_local(c, name, type, count, address);
        if (accept(c, '=')) {
            if (count) { fail(c, "Local array initializers are unsupported; assign its elements instead"); return; }
            local_address(c, address); byte(c, 0x50);
            expression_t initial = expression(c, 1); value(c, &initial);
            if (!compatible(type, initial)) fail(c, "Incompatible initializer");
            byte(c, 0x59); store_ecx(c, type);
        } else if (!count) { local_address(c, address); op2(c, 0x89, 0xc1); immediate(c, 0); store_ecx(c, type); }
    } while (accept(c, ','));
    expect(c, ';', "Expected ; after declaration");
}
static void statement(compiler_t *c);
static void block(compiler_t *c) {
    expect(c, '{', "Expected {"); uint32_t old = c->nlocals; c->scope++;
    while (token(c)->kind != '}' && token(c)->kind != T_EOF && !c->result->error) {
        if (is_type(token(c)->kind)) local_declaration(c); else statement(c);
    }
    expect(c, '}', "Expected }"); c->scope--; c->nlocals = old;
}
static void statement(compiler_t *c) {
    if (c->depth >= 64) { fail(c, "Statement nesting limit exceeded"); return; }
    c->depth++;
    if (token(c)->kind == '{') block(c);
    else if (accept(c, ';')) {}
    else if (accept(c, T_IF)) {
        expect(c, '(', "Expected ( after if"); expression_t condition = expression(c, 1); value(c, &condition);
        expect(c, ')', "Expected ) after condition"); op2(c, 0x85, 0xc0);
        uint32_t otherwise = jump(c, 0x84); statement(c);
        if (accept(c, T_ELSE)) {
            uint32_t end = jump(c, 0); target(c, otherwise, c->pc); statement(c); target(c, end, c->pc);
        } else target(c, otherwise, c->pc);
    } else if (accept(c, T_WHILE)) {
        if (c->nloops == 32) { fail(c, "Loop nesting limit exceeded"); c->depth--; return; }
        uint32_t start = c->pc; expect(c, '(', "Expected ( after while");
        expression_t condition = expression(c, 1); value(c, &condition);
        expect(c, ')', "Expected ) after condition"); op2(c, 0x85, 0xc0);
        uint32_t end = jump(c, 0x84), loop = c->nloops++;
        c->loops[loop] = (loop_t){start, NONE}; statement(c);
        target(c, jump(c, 0), start);
        target(c, end, c->pc); patch_chain(c, c->loops[loop].breaks, c->pc); c->nloops--;
    } else if (accept(c, T_RETURN)) {
        if (accept(c, ';')) {
            if (c->return_type != SZ_VOID) fail(c, "Non-void return requires a value");
            immediate(c, 0);
        } else {
            expression_t e = expression(c, 1); value(c, &e);
            if (!compatible(c->return_type, e)) fail(c, "Incompatible return type");
            truncate_char(c, c->return_type); expect(c, ';', "Expected ; after return");
        }
        c->returns = chain_jump(c, c->returns);
    } else if (token(c)->kind == T_BREAK || token(c)->kind == T_CONTINUE) {
        uint32_t kind = token(c)->kind; c->pos++;
        if (!c->nloops) fail(c, "break/continue requires a loop");
        else {
            loop_t *loop = &c->loops[c->nloops - 1];
            if (kind == T_BREAK) loop->breaks = chain_jump(c, loop->breaks);
            else target(c, jump(c, 0), loop->start);
        }
        expect(c, ';', "Expected ;");
    } else {
        expression_t e = expression(c, 1);
        if (e.type != SZ_VOID) value(c, &e);
        expect(c, ';', "Expected ; after expression");
    }
    c->depth--;
}
static void function_body(compiler_t *c, function_t *f) {
    c->pos = f->body; c->local_bytes = c->nlocals = c->nloops = 0;
    c->scope = 1; c->return_type = f->type; c->returns = NONE;
    f->address = c->pc; byte(c, 0x55); op2(c, 0x89, 0xe5);
    op2(c, 0x81, 0xec); uint32_t stack_size = c->pc; word(c, 0);
    for (uint32_t i = 0; i < f->argc; i++) add_local(c, f->params[i], f->argtypes[i], 0, 8 + i * 4);
    block(c); op2(c, 0x31, 0xc0); patch_chain(c, c->returns, c->pc);
    byte(c, 0xc9); byte(c, 0xc3);
    patch(c, stack_size, (c->local_bytes + 15) & ~15u);
}
static void entry(compiler_t *c, uint32_t main_index) {
    byte(c, 0x55); op2(c, 0x89, 0xe5); byte(c, 0x53); byte(c, 0x56); byte(c, 0x57);
    op2(c, 0x8b, 0x75); byte(c, 8); /* ESI = environment */
    op2(c, 0x8b, 0x7e); byte(c, SZ_ENV_API_OFFSET);
    op2(c, 0x85, 0xff); uint32_t bad[4]; bad[0] = jump(c, 0x84);
    op2(c, 0x81, 0x3f); word(c, SZ_API_MAGIC); bad[1] = jump(c, 0x85);
    op2(c, 0x81, 0x7f); byte(c, 4); word(c, SZ_API_VERSION); bad[2] = jump(c, 0x85);
    op2(c, 0x81, 0x7f); byte(c, 8); word(c, SZ_API_COUNT); bad[3] = jump(c, 0x82);
    byte(c, 0xe8); word(c, 0); uint32_t base = c->pc;
    byte(c, 0x5b); op2(c, 0x81, 0xeb); word(c, base);
    uint32_t argc = c->functions[main_index].argc;
    if (argc) { op2(c, 0x8b, 0x45); byte(c, 12); byte(c, 0x50); }
    call(c, argc, 0, main_index);
    uint32_t done = jump(c, 0);
    for (uint32_t i = 0; i < 4; i++) target(c, bad[i], c->pc);
    immediate(c, 2); target(c, done, c->pc);
    op2(c, 0x8d, 0x65); byte(c, 0xf4); byte(c, 0x5f); byte(c, 0x5e); byte(c, 0x5b);
    byte(c, 0xc9); byte(c, 0xc3);
}
uint32_t szc_workspace_size(void) { return sizeof(compiler_t); }
int szc_compile(const uint8_t *source, uint32_t size, uint8_t *image, uint32_t capacity,
                void *workspace, szc_result_t *result) {
    if (!result) return 0;
    *result = (szc_result_t){0};
    if (!source || size > SZC_MAX_SOURCE || !image || capacity < ENTRY || !workspace ||
        (uintptr_t)workspace % _Alignof(compiler_t)) {
        result->error = "Invalid compiler buffers or source exceeds 64 KiB";
        result->line = result->column = 1; return 0;
    }
    compiler_t *c = workspace; zero(c, sizeof *c);
    c->source = source; c->size = size; c->image = image;
    c->capacity = capacity > SZC_MAX_IMAGE ? SZC_MAX_IMAGE : capacity; c->result = result;
    lex(c); if (result->error) return 0;
    declarations(c); if (result->error) return 0;
    uint32_t main_index = NONE;
    for (uint32_t i = 0; i < c->nfunctions; i++) if (named(c, c->functions[i].name, "main")) main_index = i;
    if (main_index == NONE || !c->functions[main_index].defined) fail(c, "Program requires an int main() function");
    else {
        function_t *main = &c->functions[main_index];
        if (main->type != SZ_INT || main->argc > 1 || (main->argc && main->argtypes[0] != SZ_CHAR_PTR))
            fail(c, "main must return int and take zero arguments or one char* argument");
    }
    if (result->error) return 0;
    zero(image, ENTRY); c->pc = ENTRY; entry(c, main_index);
    for (uint32_t i = 0; i < c->nfunctions && !result->error; i++)
        if (c->functions[i].defined) function_body(c, &c->functions[i]);
    for (uint32_t i = 0; i < c->nfunctions && !result->error; i++) if (c->functions[i].referenced) {
        c->functions[i].thread_address = c->pc; entry(c, i);
    }
    if (result->error) return 0;
    while (c->pc % 4) byte(c, 0);
    uint32_t data_start = c->pc;
    for (uint32_t i = 0; i < c->data_size && !result->error; i++) byte(c, c->data[i]);
    for (uint32_t i = 0; i < c->ndatafix && !result->error; i++)
        patch(c, c->datafix[i].at, data_start + c->datafix[i].target);
    for (uint32_t i = 0; i < c->ncallfix && !result->error; i++) {
        function_t *f = &c->functions[c->callfix[i].target];
        if (!f->defined) fail(c, "Called function has no definition");
        else target(c, c->callfix[i].at, f->address);
    }
    for (uint32_t i = 0; i < c->nfunctionfix && !result->error; i++) {
        function_t *f = &c->functions[c->functionfix[i].target];
        if (!f->defined) fail(c, "Thread function has no definition");
        else patch(c, c->functionfix[i].at, f->thread_address);
    }
    if (result->error) return 0;
    /* ELF32 ET_DYN with one PT_LOAD, vaddr/offset zero. All addresses are relative
       to EBX; calls use relative displacements, so no relocation section is needed. */
    image[0] = 0x7f; image[1] = 'E'; image[2] = 'L'; image[3] = 'F';
    image[4] = image[5] = image[6] = 1;
    image[16] = 3; image[18] = 3; put32(image + 20, 1);
    put32(image + 24, ENTRY); put32(image + 28, 52);
    image[40] = 52; image[42] = 32; image[44] = 1;
    put32(image + 52, 1); put32(image + 68, c->pc); put32(image + 72, c->pc);
    put32(image + 76, 7); put32(image + 80, 4096);
    result->size = c->pc; return 1;
}
