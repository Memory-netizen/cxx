#!/bin/bash
# E2: -Wimplicit-fallthrough.
#
# One file per shape, compiled by gcc, clang and cxx. The two references
# answer differently on a good third of these -- gcc's check runs late and
# keys on a statement, clang's runs on its CFG -- so the point of the table
# is not "match one of them" but to write down, shape by shape, what cxx
# does and why. `want` is the documented cxx answer; the script exits
# non-zero if cxx ever disagrees with it.
#
# gcc only diagnoses this on the code-generation path, so it is run with -c.
C=${1:-/home/memory/cxx/cxx}
t=`mktemp -d /tmp/cxx-ft-XXXXXX`
trap 'rm -rf $t' EXIT

n=0
bad=0

gcc_n=0
clang_n=0
cxx_n=0

# c <name> <want> <note>  -- body of one switch statement on stdin
c() {
    local name=$1 want=$2 note=$3
    {
        printf 'int x, y;\n_Noreturn void noret(void);\nint f(void) {\n  switch (x) {\n'
        cat
        printf '  }\n  return 0;\n}\n'
    } > "$t/$name.c"

    local g cl x
    g=$(gcc -std=c23 -c -o /dev/null -Wimplicit-fallthrough "$t/$name.c" 2>&1 | grep -c 'this statement may fall through')
    cl=$(clang -std=c23 -fsyntax-only -Wimplicit-fallthrough "$t/$name.c" 2>&1 | grep -c 'unannotated fall-through')
    x=$("$C" -Wimplicit-fallthrough -S -o /dev/null "$t/$name.c" 2>&1 | grep -c 'unannotated fall-through')

    n=$((n + 1))
    gcc_n=$((gcc_n + g))
    clang_n=$((clang_n + cl))
    cxx_n=$((cxx_n + x))
    local mark=""
    if [ "$x" != "$want" ]; then
        mark="  <<< cxx $x, documented $want"
        bad=$((bad + 1))
    elif [ "$g" = "$x" ] && [ "$cl" != "$x" ]; then
        mark="  (= gcc)"
    elif [ "$cl" = "$x" ] && [ "$g" != "$x" ]; then
        mark="  (= clang)"
    fi
    printf '%-24s cxx=%s gcc=%s clang=%s  %s%s\n' "$name" "$x" "$g" "$cl" "$note" "$mark"
}

echo "=== one shape per file: cxx against the documented answer"
echo

c plain 1 "the shape the warning exists for" <<'EOF'
  case 1: y = 1;
  case 2: return 0;
EOF

c adjacent 0 "a run of labels is one entry point" <<'EOF'
  case 1: case 2: return 0;
EOF

c empty_stmt 1 "a fall into a label whose body is empty: neither reference reports it" <<'EOF'
  case 1: y = 1;
  case 2: ;
EOF

c empty_seq 1 "an empty statement still falls through (clang agrees)" <<'EOF'
  case 1: ;
  case 2: return 0;
EOF

c empty_comp 1 "an empty block still falls through (clang agrees)" <<'EOF'
  case 1: {}
  case 2: return 0;
EOF

c jump_return 0 "return leaves" <<'EOF'
  case 1: return 1;
  case 2: return 0;
EOF

c jump_break 0 "break leaves" <<'EOF'
  case 1: break;
  case 2: return 0;
EOF

c jump_goto 0 "goto leaves" <<'EOF'
  case 1: goto out;
  case 2: return 0;
out:
EOF

c if_plain 1 "the false path of an if falls through" <<'EOF'
  case 1: if (x) return 1;
  case 2: return 0;
EOF

c if_else_both_ret 0 "both branches leave" <<'EOF'
  case 1: if (x) return 1; else return 2;
  case 2: return 0;
EOF

c else_if_chain 0 "the chain's last else leaves" <<'EOF'
  case 1: if (x) return 1; else if (y) return 2; else return 3;
  case 2: return 0;
EOF

c if_then_else_empty 1 "the empty then-branch is a path past the if" <<'EOF'
  case 1: if (x) ; else return 3;
  case 2: return 0;
EOF

c while_1 0 "an endless loop never falls out" <<'EOF'
  case 1: while (1) {}
  case 2: return 0;
EOF

c while_cond 1 "a real condition can be false" <<'EOF'
  case 1: while (x) {}
  case 2: return 0;
EOF

c for_ever 0 "for (;;) is endless too" <<'EOF'
  case 1: for (;;) {}
  case 2: return 0;
EOF

c while1_break 1 "a break leaves the endless loop; clang agrees, gcc misses it" <<'EOF'
  case 1: while (1) { break; }
  case 2: return 0;
EOF

c for_no_cond_break 1 "same for for (;;)" <<'EOF'
  case 1: for (;;) { if (x) break; }
  case 2: return 0;
EOF

c do1_break 1 "a do-while is entered once, the break leaves" <<'EOF'
  case 1: do { break; } while (1);
  case 2: return 0;
EOF

c do_while0 1 "do-while with a false condition on the first pass" <<'EOF'
  case 1: do { y = 1; } while (0);
  case 2: return 0;
EOF

c while1_sw_break 0 "that break leaves the switch, not the loop" <<'EOF'
  case 1: while (1) { switch (y) { case 5: break; } }
  case 2: return 0;
EOF

c while_break 1 "a plain loop with a break falls out" <<'EOF'
  case 1: while (x) { break; }
  case 2: return 0;
EOF

c inner_sw_break 1 "the inner switch hands control on" <<'EOF'
  case 1: switch (y) { default: break; }
  case 2: return 0;
EOF

c inner_sw_ret 0 "every path of the inner switch returns" <<'EOF'
  case 1: switch (y) { default: return 3; }
  case 2: return 0;
EOF

c inner_sw_nodefault 1 "no default: an unmatched value falls out (gcc agrees)" <<'EOF'
  case 1: switch (y) { case 5: return 1; }
  case 2: return 0;
EOF

c noret_call 0 "a function declared noreturn never comes back" <<'EOF'
  case 1: noret();
  case 2: return 0;
EOF

c noret_in_if 1 "the other path of the if still falls through" <<'EOF'
  case 1: if (x) noret();
  case 2: return 0;
EOF

c annot_c23 0 "[[fallthrough]] says the fall is deliberate" <<'EOF'
  case 1: [[fallthrough]];
  case 2: return 0;
EOF

c annot_gnu 0 "the GNU spelling does the same" <<'EOF'
  case 1: __attribute__((fallthrough));
  case 2: return 0;
EOF

c annot_then_stmt 1 "the annotation covers one fall, not the next statement" <<'EOF'
  case 1: [[fallthrough]]; y = 1;
  case 2: return 0;
EOF

c comment 1 "a comment is not an annotation (clang agrees; gcc reads comments)" <<'EOF'
  case 1: y = 1; /* fall through */
  case 2: return 0;
EOF

c block 1 "a block's last statement is the case's" <<'EOF'
  case 1: { y = 1; }
  case 2: return 0;
EOF

c block_break 0 "a block ending in break leaves" <<'EOF'
  case 1: { break; }
  case 2: return 0;
EOF

c nested_block 1 "blocks do not hide a fall-through" <<'EOF'
  case 1: { { y = 1; } }
  case 2: return 0;
EOF

c label_inside 1 "a case label inside a block is still a label" <<'EOF'
  case 1: { y = 1;
  case 2: return 0; }
EOF

c user_label 0 "a label leads into the case; clang agrees, gcc warns" <<'EOF'
  case 1: y = 1;
  l1:
  case 2: return 0;
EOF

c user_label_stmt 1 "but a statement after the label falls through" <<'EOF'
  case 1: y = 1;
  l2: y = 2;
  case 2: return 0;
EOF

c goto_label_case 0 "a goto straight to the label is not a fall-through" <<'EOF'
  case 1: goto l3;
  l3:
  case 2: return 0;
EOF

c default_mid 1 "default is a label like any other" <<'EOF'
  case 1: y = 1;
  default: return 0;
EOF

c default_first 1 "and it can be the one fallen into" <<'EOF'
  default: y = 1;
  case 2: return 0;
EOF

c last_no_label 0 "nothing after the last label, nothing to warn about" <<'EOF'
  case 1: y = 1;
EOF

c triple_label 1 "only the last label of a run is fallen into" <<'EOF'
  case 1: case 2: y = 1;
  case 3: return 0;
EOF

c multi_label_fall 1 "labels stacked above the next case" <<'EOF'
  case 1: y = 1;
  case 2:
  case 3: return 0;
EOF

c unreachable_after_ret 1 "a dead label is not reached, the next one is" <<'EOF'
  case 1: return 1;
  case 2: y = 2;
  case 3: return 0;
EOF

c dead_after_goto 1 "unreachable code is not modelled; gcc agrees, clang misses it" <<'EOF'
  case 1: goto out2; y = 1;
  case 2: return 0;
out2:
EOF

c label_empty_at_end 2 "falls into the label, then out of the block" <<'EOF'
  case 1: { y = 1;
  case 2: }
  case 3: return 0;
EOF

c static_assert_stmt 1 "a static assertion is not a jump" <<'EOF'
  case 1: _Static_assert(1, "ok");
  case 2: return 0;
EOF

c decl_between 2 "a declaration does not end the run of statements" <<'EOF'
  case 1: y = 1;
  case 2: { int z = 5; y = z; }
  case 3: return 0;
EOF

c nested_loops 1 "continue and break inside nested loops" <<'EOF'
  case 1: while (x) { if (y) break; else continue; }
  case 2: return 0;
EOF

c cont_in_loop 1 "continue does not leave the loop" <<'EOF'
  case 1: for (y = 0; y < 3; y++) { continue; }
  case 2: return 0;
EOF

c brace_body 0 "a switch body need not be a block" <<'EOF'
  case 1: y = 1;
EOF


echo
echo "=== the group is off unless asked for, as in clang"
cat > "$t/group.c" <<'EOF'
int f(int x) {
    int n = 0;
    switch (x) {
    case 1: n++;
    case 2: n--;
    }
    return n;
}
EOF
for f in "" "-Wall" "-Wextra" "-w" "-Wno-implicit-fallthrough" "-Wimplicit-fallthrough"; do
    n_warn=$("$C" $f -S -o /dev/null "$t/group.c" 2>&1 | grep -c 'unannotated fall-through')
    printf '  %-28s %s\n' "[${f:-<default>}]" "$n_warn"
done
n_err=$("$C" -Wimplicit-fallthrough -Werror -S -o /dev/null "$t/group.c" 2>&1 | grep -c 'error: unannotated fall-through')
printf '  %-28s %s\n' "[-Wimplicit-fallthrough -Werror]" "$n_err"
printf '  %-28s %s\n' "[unknown group]" "$("$C" -Wnot-a-group -S -o /dev/null "$t/group.c" 2>&1 | grep -c 'unknown warning group')"

echo
echo "=== totals over $n shapes: gcc $gcc_n, clang $clang_n, cxx $cxx_n warnings"
echo "    cxx mismatches against the documented answer: $bad"
exit $((bad > 0))
