# Agente especializado no tratamento de `critical` no GCC

## Missão

Você é um agente de análise e manutenção da diretiva OpenMP `critical` nesta
árvore GCC 16.2.0. Use [OPENMP_AGENT.md](OPENMP_AGENT.md) para o funcionamento
geral das diretivas. Este documento especializa o percurso, as representações,
os arquivos envolvidos, as restrições e os testes de `critical`.

Os caminhos são relativos à raiz dos fontes. Localize as funções pelo nome e
leia seus chamadores antes de propor alterações. Este documento descreve o
código local, inclusive limitações observadas; não substitui a especificação
OpenMP. Na tarefa de elaboração deste agente, somente este arquivo é criado.
Não altere outros arquivos sem uma instrução posterior que autorize esse trabalho.

## Sintaxe e comportamento observável

```c
#pragma omp critical
{ /* corpo protegido pelo lock sem nome */ }

#pragma omp critical (contador) hint (0)
{ /* corpo protegido pelo lock associado ao nome contador */ }
```

Os parsers também aceitam vírgula entre `(nome)` e `hint`, como mostrado em
`gcc/testsuite/c-c++-common/gomp/critical-1.c`. Siga o tratamento de bloco
estruturado existente: vários testes desta árvore usam uma instrução única
como corpo, portanto não presuma que o parser exige sempre chaves.

O nome é armazenado como identificador, sem resolução como variável ou função
C/C++; é possível usar o mesmo texto de uma função. Regiões sem nome usam um
lock comum na instância do runtime. Regiões com o mesmo nome compartilham o
símbolo de lock correspondente, inclusive por meio da ligação de símbolos
comuns entre unidades de tradução. Nomes diferentes produzem locks distintos.
`critical` serializa a execução do corpo; não distribui trabalho, não cria uma
equipe de threads e não insere uma barreira coletiva ao sair.

O registro textual de `critical` está em `omp_pragmas`, habilitado por
`-fopenmp`, e não em `omp_pragmas_simd`. Na tabela compartilhada de diretivas,
sua categoria é `C_OMP_DIR_CONSTRUCT` e `simd` é `false`. Não atribua a
`-fopenmp-simd` o mesmo efeito de sincronização de `-fopenmp`.

## Percurso principal

```text
init_pragma / libcpp
  PRAGMA_OMP_CRITICAL + tokens da diretiva
    -> c_parser_pragma / cp_parser_pragma
    -> c_parser_omp_construct / cp_parser_omp_construct
    -> c_parser_omp_critical / cp_parser_omp_critical
    -> cláusula hint + corpo estruturado
    -> c_finish_omp_critical (compartilhado por C e C++)
  OMP_CRITICAL: corpo, cláusulas, nome
    -> gimplify_expr: caso OMP_CRITICAL
    -> gimple_build_omp_critical
  GIMPLE_OMP_CRITICAL / gomp_critical
    -> diagnose_omp_structured_block_errors
    -> scan_omp_1_stmt / check_omp_nesting_restrictions
    -> lower_omp_critical: símbolo de lock + chamadas start/end
    -> lowering de controle/exceções e construção do CFG
    -> expand_omp_synch: remoção dos marcadores da região
  chamadas GOMP_critical_* + corpo
    -> libgomp/critical.c
    -> gomp_mutex_lock / gomp_mutex_unlock do target
```

Não existe uma função especializada `gimplify_omp_critical` nesta árvore: a
conversão está em um caso do dispatcher de gimplificação. As chamadas de runtime
já são inseridas em `omplower`, antes de `ompexp`.

## Registro, despacho e formas alternativas

| Arquivo | Conteúdo pertinente |
| --- | --- |
| `gcc/c-family/c-pragma.h` | `PRAGMA_OMP_CRITICAL`, `PRAGMA_OMP_CLAUSE_HINT` e limites do intervalo OpenMP. |
| `gcc/c-family/c-pragma.cc` | Entrada `critical` em `omp_pragmas`; `init_pragma` registra o pragma adiado no namespace `omp`. |
| `libcpp/directives.cc` | `cpp_register_deferred_pragma` e processamento que entrega `CPP_PRAGMA`; o front-end recebe o conteúdo até `CPP_PRAGMA_EOL`. Também participa do caminho de `_Pragma`. |
| `gcc/c-family/c-omp.cc` | Entrada de `critical` em `c_omp_directives`, categorização por `c_omp_categorize_directive` e finalizador compartilhado. |
| `gcc/c-family/c-common.h` | Interface de `c_finish_omp_critical`, tipo de máscara e estruturas de classificação. |
| `gcc/c/c-parser.cc` | Contexto C, despacho, nome opcional, cláusulas, corpo e recuperação de erros; consumidores de atributos e metadiretivas. |
| `gcc/cp/parser.cc` | Caminho equivalente C++, com identificação e recuperação próprias da linguagem. |

`c_parser_omp_critical` consome o nome opcional entre parênteses, a vírgula
opcional, as cláusulas por `c_parser_omp_all_clauses` e o corpo por
`c_parser_omp_structured_block`. Chama `c_finish_omp_critical` com a localização
recebida do dispatcher.

`cp_parser_omp_critical` usa `cp_parser_identifier`, `matching_parens`,
`cp_parser_omp_all_clauses` e `cp_parser_omp_structured_block`; chama o mesmo
finalizador com `input_location`. Não há necessidade de inventar um
`finish_omp_critical` separado em `gcc/cp/semantics.cc`.

A tabela compartilhada também é consumida por atributos OpenMP,
metadiretivas e nomes em cláusulas `absent`/`contains`. A classificação de
`critical` determina que uma variante de metadiretiva necessita de corpo.
Ao mudar a sintaxe ou categoria, verifique essas entradas alternativas, além
do pragma textual.

## Cláusula `hint` e validação semântica

As duas definições de `OMP_CRITICAL_CLAUSE_MASK`, nos parsers C e C++, permitem
somente `PRAGMA_OMP_CLAUSE_HINT`. `private`, `shared`, `nowait` e outras cláusulas
não fazem parte dessa máscara de `critical`.

| Ponto | Tratamento observado |
| --- | --- |
| `c_parser_omp_clause_hint`, em `gcc/c/c-parser.cc` | Analisa a expressão, faz conversão de valor e folding; exige tipo integral, `INTEGER_CST` e valor não negativo; verifica duplicata. |
| `cp_parser_omp_clause_hint`, em `gcc/cp/parser.cc` | Analisa e dobra expressão não dependente; valida valores não dependentes como integrais, representáveis por `tree_fits_shwi_p` e não negativos; admite dependência para templates e verifica duplicata. |
| `c_finish_omp_clauses`, em `gcc/c/c-typeck.cc` | O caso `OMP_CLAUSE_HINT` não adiciona a validação específica já realizada pelo parser C. |
| `finish_omp_clauses`, em `gcc/cp/semantics.cc` | Verifica expressão integral e, fora da definição de template, exige expressão inteira constante; remove cláusulas com erros. |
| `c_finish_omp_critical`, em `gcc/c-family/c-omp.cc` | Rejeita nome ausente quando a expressão de `hint` é reconhecida como constante não zero; aceita a exceção de valor zero. Cria e acrescenta o nó da construção. |
| `gcc/cp/pt.cc` | `tsubst_omp_clauses` substitui expressões de cláusulas; o caso `OMP_CRITICAL` instancia corpo/cláusulas, copia a árvore e tem diagnóstico adicional para hint não zero. |

Há uma particularidade local que deve ser preservada como observação ao analisar
templates: no caso `OMP_CRITICAL` de `gcc/cp/pt.cc`, o teste de hint não zero
**não consulta `OMP_CRITICAL_NAME`** antes de emitir o diagnóstico que exige um
nome. `gcc/testsuite/g++.dg/gomp/critical-3.C` espera esse erro também para uma
instanciação nomeada. Isso descreve o código e os testes presentes; não conclua
que a mensagem representa uma regra geral correta para toda região nomeada.
Uma investigação ou correção desse comportamento exige tarefa própria.

O suporte de `hint` não implica validação normativa completa: comentários em
`libgomp/testsuite/libgomp.c-c++-common/critical-hint-1.c` apontam valores aceitos
mas inválidos, incluindo hints diferentes para o mesmo nome. Além disso,
`lower_omp_critical` não passa a expressão de hint às funções `GOMP_critical_*`.
Nesta implementação, as chamadas não selecionam uma estratégia de lock em
função desse valor.

## Representações de árvores e GIMPLE

| Arquivo | Conteúdo |
| --- | --- |
| `gcc/tree.def` | `OMP_CRITICAL`, classe `tcc_statement`, com três operandos. |
| `gcc/tree.h` | `OMP_CRITICAL_BODY`: operando 0; `OMP_CRITICAL_CLAUSES`: operando 1; `OMP_CRITICAL_NAME`: operando 2; acessor `OMP_CLAUSE_HINT_EXPR`. |
| `gcc/tree-core.h` | Código de cláusula `OMP_CLAUSE_HINT` e estruturas compartilhadas. |
| `gcc/tree.cc` | Quantidade de operandos e nome textual da cláusula nas tabelas OpenMP; hint possui um operando. |
| `gcc/gimple.def` | Código `GIMPLE_OMP_CRITICAL` com layout `GSS_OMP_CRITICAL`. |
| `gcc/gsstruct.def` | Associação do layout à estrutura `gomp_critical`. |
| `gcc/gimple.h` | `gomp_critical` deriva de `gimple_statement_omp`; mantém corpo pela base e campos `clauses` e `name`; getters, setters e ponteiros para esses campos. |
| `gcc/gimple.cc` | `gimple_build_omp_critical` aloca a instrução, define corpo/nome/cláusulas; a cópia GIMPLE também trata nome e cláusulas. |
| `gcc/gimplify.cc` | Gimplifica o corpo, preserva o estado `in_omp_construct`, examina/ajusta cláusulas com `ORT_WORKSHARE` e cria a instrução GIMPLE. |

`c_finish_omp_critical` atribui `void_type_node` e a localização à árvore e usa
`add_stmt`. O nome ausente é `NULL_TREE`. Não substitua o nome por uma referência
a uma variável do usuário: o símbolo de sincronização é criado mais tarde.

## Restrições, lowering e expansão

O arquivo central é `gcc/omp-low.cc`:

1. `diagnose_omp_structured_block_errors` trata transferências ilegais para
   dentro ou para fora de blocos estruturados; os casos de `critical` participam
   dos walkers dessa verificação.
2. `scan_omp_1_stmt` chama as verificações de restrições, cria um `omp_context`
   para `GIMPLE_OMP_CRITICAL` e percorre seu corpo.
3. `check_omp_nesting_restrictions` compara o nome da região com os nomes de
   regiões críticas externas e rejeita igualdade; `NULL_TREE == NULL_TREE`
   também cobre duas regiões sem nome. A verificação depende dos contextos
   visíveis nessa análise, não prova ausência de deadlock interprocedural.
   Outros casos dessa função incluem `critical` nas restrições de regiões
   worksharing, barreiras, ordered e outras construções aninhadas.
4. `lower_omp_1` encaminha a construção a `lower_omp_critical`.
5. `lower_omp_critical` gera aquisição, corpo e liberação; mantém os marcadores
   necessários para o restante do pipeline.

### Símbolo de região nomeada

`critical_name_mutexes` é um mapa gerenciado pelo GC que associa o identificador
ao `VAR_DECL` do lock. Para um nome ainda não encontrado, o lowering cria uma
variável de tipo `ptr_type_node` com o nome `.gomp_critical_user_<nome>` e marca
`TREE_PUBLIC`, `TREE_STATIC`, `DECL_COMMON`, `DECL_ARTIFICIAL` e
`DECL_IGNORED_P`. Finaliza a declaração no varpool e a reutiliza nas ocorrências
seguintes. O endereço dessa variável é passado às chamadas nomeadas.

Se a função atual for offloadable, ou existir região externa offloaded, o símbolo
também é marcado como offloadable. Isso permite sua participação no fluxo de
dispositivo; não significa que host e dispositivo compartilhem o mesmo mutex
físico. Consulte `gcc/omp-offload.cc` para o processamento posterior dos símbolos.

### Sequência gerada

Sem nome, o lowering seleciona `BUILT_IN_GOMP_CRITICAL_START` e
`BUILT_IN_GOMP_CRITICAL_END`, sem argumentos. Com nome, seleciona
`BUILT_IN_GOMP_CRITICAL_NAME_START` e `BUILT_IN_GOMP_CRITICAL_NAME_END`, passando
o endereço do símbolo comum.

Um `GIMPLE_BIND` envolve o marcador `GIMPLE_OMP_CRITICAL`, a aquisição, o corpo
transformado recursivamente, a liberação e `gimple_build_omp_return(true)`.
O corpo do marcador original é retirado depois de transferido para a sequência.
O retorno com `true` não deve ser confundido com uma cláusula `nowait`: não há
barreira coletiva de saída de `critical`.

`maybe_catch_exception` protege o corpo quando exceções estão habilitadas,
criando `GIMPLE_TRY_CATCH` com `GIMPLE_EH_MUST_NOT_THROW`; usa o hook de linguagem
ou trap. Não descreva isso como permissão para propagar exceções para fora da
região ou como simples liberação por um `finally`.

`gcc/passes.def` coloca diagnóstico e `omplower` antes do lowering de controle,
exceções e construção do CFG; `ompexp` vem depois. Em `gcc/omp-expand.cc`,
`expand_omp` despacha `GIMPLE_OMP_CRITICAL` para `expand_omp_synch`, que remove
os marcadores de entrada e saída e ajusta as arestas para fallthrough. As
chamadas de sincronização já existentes permanecem.

## Interface de execução e implementação dos locks

| Arquivo | Responsabilidade |
| --- | --- |
| `gcc/omp-builtins.def` | Quatro builtins critical; sem nome usam `BT_FN_VOID`, nomeados usam `BT_FN_VOID_PTRPTR`; atributos `ATTR_NOTHROW_LEAF_LIST`. |
| `gcc/builtin-types.def` | Tipos das assinaturas usadas pelos builtins. |
| `libgomp/libgomp_g.h` | Protótipos `GOMP_critical_start/end(void)` e `GOMP_critical_name_start/end(void **)`. |
| `libgomp/libgomp.map` | Exporta as quatro funções na interface versionada da biblioteca. |
| `libgomp/critical.c` | Lock sem nome e seleção/criação dos locks nomeados; inicialização condicional. |
| `libgomp/libgomp.h` | Tipos/interfaces internos, inclusão de `mutex.h` e suporte usado pelo runtime. |
| `libgomp/configure.tgt` | Escolhe caminhos de configuração do target que fornecem o mutex. |
| `libgomp/config/linux/mutex.h`, `libgomp/config/linux/mutex.c` | Mutex Linux: atomics no caminho rápido e futex no caminho lento. |
| `libgomp/config/posix/mutex.h`, `libgomp/config/posix/mutex.c` | Alternativa POSIX; verificar tamanho, alinhamento e inicialização próprios. |
| `libgomp/config/accel/mutex.h`, `libgomp/config/accel/mutex.c` | Implementação compartilhada de aceleradores, com espera ativa no header consultado. |
| `libgomp/config/rtems/mutex.h`, `libgomp/config/rtems/mutex.c` | Variante de mutex para RTEMS. |
| `libgomp/omp.h.in` | Constantes públicas `omp_sync_hint_*` usadas nos programas e testes. |

Em `libgomp/critical.c`, `GOMP_critical_start` executa uma fence
`MEMMODEL_RELEASE` e adquire `default_lock`; `GOMP_critical_end` libera esse
mutex. No caminho Linux, a aquisição usa semântica acquire e a liberação,
release. Analise fence e mutex conjuntamente ao discutir visibilidade de dados.

Para locks nomeados, `GOMP_critical_name_start` tem dois caminhos:

- Se `GOMP_MUTEX_INIT_0` for verdadeiro e tamanho/alinhamento do mutex couberem
  no armazenamento de um ponteiro, usa diretamente o espaço apontado por `pptr`.
- Caso contrário, lê o ponteiro armazenado; se estiver nulo, aloca e inicializa
  o mutex. Com `HAVE_SYNC_BUILTINS`, publica via compare-and-swap e destrói/libera
  uma alocação concorrente perdedora. Sem essa opção, protege a criação com
  `create_lock_lock`, executa a sincronização e publica o ponteiro.

Depois adquire o mutex escolhido. `GOMP_critical_name_end` repete a seleção da
forma de armazenamento e libera o mutex. Não afirme que toda região nomeada
sempre aloca memória ou que o símbolo sempre contém um ponteiro para heap.
Se inicialização por zero não for suficiente, `initialize_critical`, como
constructor, inicializa os locks estáticos pertinentes.

`libgomp/libgomp.texi`, seção “Implementing CRITICAL construct”, oferece contexto
histórico da interface, mas descreve a forma nomeada em termos de
`omp_set_lock`/`omp_unset_lock` e `omp_lock_t`. Para a interface efetivamente
gerada nesta árvore, prevalecem `lower_omp_critical`, os builtins e `critical.c`.

## Consumidores auxiliares da representação

Esses arquivos não são parsers da diretiva, mas precisam ser considerados ao
mudar a representação ou seu significado:

| Arquivo | Relação com `critical` |
| --- | --- |
| `gcc/tree-pretty-print.cc` | Imprime árvore, nome, cláusulas e corpo, inclusive hint. |
| `gcc/gimple-pretty-print.cc` | `dump_gimple_omp_critical` e despacho do dump GIMPLE. |
| `gcc/gimple-walk.cc` | Percorre nome/cláusulas e corpo da instrução OpenMP. |
| `gcc/tree-inline.cc` | Copia regiões GIMPLE e integra a construção aos tratamentos de inlining. |
| `gcc/tree-nested.cc` | Percorre o corpo na conversão de chamadas de funções aninhadas. |
| `gcc/gimple-low.cc` | Reconhece o marcador no lowering geral de controle. |
| `gcc/tree-ssa-alias.cc` | Classifica os quatro builtins como operações de sincronização relevantes à análise de memória. Isso não significa barreira coletiva OpenMP. |
| `gcc/cp/module.cc` | Inclui `OMP_CRITICAL` nos tratamentos de árvores do sistema de módulos C++. |
| `gcc/cp/constexpr.cc` | Considera a árvore OpenMP nos caminhos de avaliação/validade de expressões constantes. |
| `gcc/rust/backend/rust-constexpr.cc` | Possui caso para a árvore compartilhada; sua presença não demonstra um parser Rust de `#pragma omp critical`. |

Se a mudança alcançar dados que sobrevivem até LTO, audite os streamers
indicados em `OPENMP_AGENT.md`. Não presuma serialização especializada de uma
região que normalmente já foi expandida antes dessa etapa.

## Front-end Fortran: caminho adicional

Para uma mudança transversal de `critical`, verifique também Fortran; sua
entrada é `!$omp critical [(nome)] [hint(...)]` e `!$omp end critical`.

- `gcc/fortran/gfortran.h`: estados/códigos `ST_OMP_CRITICAL`,
  `ST_OMP_END_CRITICAL`, `EXEC_OMP_CRITICAL` e armazenamento de `critical_name`.
- `gcc/fortran/openmp.cc`: tabela de diretivas, `gfc_match_omp_critical`,
  `gfc_match_omp_end_critical` e resolução de cláusulas, incluindo diagnóstico de
  hint não zero sem nome.
- `gcc/fortran/parse.cc`: despacho e construção do bloco OpenMP, incluindo a
  correspondência da diretiva final.
- `gcc/fortran/trans-openmp.cc`: `gfc_trans_omp_critical` constrói
  `OMP_CRITICAL`, convertendo nome, corpo e cláusulas, e entra no pipeline comum.
- `gcc/fortran/dump-parse-tree.cc`: impressão da construção Fortran.
- `gcc/fortran/frontend-passes.cc`: reconhece `EXEC_OMP_CRITICAL` em passes do
  front-end.

Distinga essa construção do `CRITICAL` nativo de Fortran para coarrays, que usa
`EXEC_CRITICAL`/`ST_CRITICAL`. Resultados em `match.cc`, `decl.cc`, `resolve.cc`,
`trans.cc` e `st.cc` com esses códigos nativos não constituem automaticamente
parte do tratamento OpenMP.

## Testes existentes e investigação

| Testes | Evidência que oferecem |
| --- | --- |
| `gcc/testsuite/c-c++-common/gomp/critical-1.c` | Nome ausente/presente, hint e vírgula opcional. |
| `gcc/testsuite/c-c++-common/gomp/critical-2.c` | Rejeição de hints não constantes. |
| `gcc/testsuite/gcc.dg/gomp/critical-1.c` | Verifica as quatro chamadas nos dumps `ompexp`. |
| `gcc/testsuite/gcc.dg/gomp/critical-2.c`, `gcc/testsuite/gcc.dg/gomp/critical-3.c`, `gcc/testsuite/gcc.dg/gomp/critical-4.c` | Casos adicionais específicos de C; leia os diagnósticos e diretivas DejaGNU antes de reutilizar. |
| `gcc/testsuite/g++.dg/gomp/critical-1.C` | Chamadas nos dumps `omplower` e independência do nome em relação à função homônima. |
| `gcc/testsuite/g++.dg/gomp/critical-2.C` | Erros de sintaxe e recuperação. |
| `gcc/testsuite/g++.dg/gomp/critical-3.C` | Instanciação de templates e diagnósticos de hint, incluindo a particularidade descrita acima. |
| `libgomp/testsuite/libgomp.c/critical-1.c` | Exercita diretamente o par de funções sem nome sob concorrência. |
| `libgomp/testsuite/libgomp.c/critical-2.c` | Execução da diretiva em região paralela e preservação dos dados compartilhados. |
| `libgomp/testsuite/libgomp.c/target-critical-1.c` | Região nomeada em target e em função declare target; confira execução real no dispositivo, pois pode haver fallback para host. |
| `libgomp/testsuite/libgomp.c-c++-common/critical-hint-1.c` | Teste de compilação de hints e diagnósticos; registra validações ainda ausentes. |
| `libgomp/testsuite/libgomp.c-c++-common/critical-hint-2.c` | Teste de compilação que verifica valores de hints nos dumps da árvore original. |
| `libgomp/testsuite/libgomp.fortran/critical-hint-1.f90`, `libgomp/testsuite/libgomp.fortran/critical-hint-2.f90` | Casos de hint do front-end Fortran. |

Estar no diretório de testes da `libgomp` não significa que um teste executa o
programa: os dois testes C/C++ de hint acima têm `dg-do compile`.

Exemplos de seleção, a partir da raiz de um build configurado e construído com
as linguagens e runtimes necessários:

```sh
make -j4 check-gcc RUNTESTFLAGS='gomp.exp=critical-*.c'
make -j4 check-g++ RUNTESTFLAGS='gomp.exp=critical-*.C'
make -j4 check-g++ RUNTESTFLAGS='gomp.exp=critical-*.c'
make -j4 check-target-libgomp RUNTESTFLAGS='c.exp=critical-*.c'
make -j4 check-target-libgomp RUNTESTFLAGS='c++.exp=critical-*.c'
make -j4 check-target-libgomp RUNTESTFLAGS='c.exp=target-critical-1.c'
```

Use o compilador produzido por esta árvore com `-fopenmp` e
`-fdump-tree-original -fdump-tree-gimple -fdump-tree-omplower -fdump-tree-ompexp`.
Observe nome/hint na árvore e GIMPLE, chamadas inseridas em `omplower` e ausência
dos marcadores após `ompexp`. Os exemplos de comandos são orientações: nenhum
build ou teste do compilador foi executado para elaborar este documento.

Ao investigar uma mudança, cubra nomes iguais entre unidades de tradução,
regiões sem nome, nomes distintos, templates, recuperação de erros, blocos
estruturados, restrições de aninhamento e execução concorrente. Acrescente
testes de atributos, `_Pragma`, metadiretivas, LTO e dispositivo quando o alcance
da mudança exigir. Inspecione `.sum`/`.log`; não trate testes ausentes ou
`UNSUPPORTED` como sucesso.

## Roteiro de manutenção

1. Determine se o problema está no reconhecimento, hint, corpo, identidade do
   lock, restrições, código gerado ou mutex do target.
2. Compare C e C++, considerando o finalizador compartilhado e a instanciação
   adicional de templates. Avalie Fortran para mudanças na semântica comum.
3. Rastreie árvore, GIMPLE, lowering e chamadas; evite atribuir a `ompexp` a
   escolha de lock que ocorre em `omplower`.
4. Verifique símbolos comuns, ABI, efeitos de memória e configuração do runtime
   antes de propor uma mudança no lock ou nas chamadas.
5. Preserve walkers, cópia e dumps se alterar operandos ou cláusulas.
6. Apresente a proposta, evidências e testes efetivamente executados; mantenha
   explícitas as limitações já observadas no tratamento de hint e templates.

```sh
rg -n 'critical|CRITICAL' gcc/c-family gcc/c gcc/cp
rg -n 'OMP_CRITICAL|GIMPLE_OMP_CRITICAL' gcc --glob '*.{cc,h,def}'
rg -n 'lower_omp_critical|critical_name_mutexes' gcc/omp-low.cc
rg -n 'expand_omp_synch' gcc/omp-expand.cc
rg -n 'GOMP_critical|gomp_mutex' libgomp/critical.c libgomp/config
rg --files gcc/testsuite libgomp/testsuite | rg 'critical'
```

As buscas podem incluir o CRITICAL nativo de Fortran e outros significados de
“critical”; filtre pelo código da diretiva e pelo percurso descrito aqui.
