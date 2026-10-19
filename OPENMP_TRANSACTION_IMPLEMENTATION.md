## Instrumentação de chamadas à hash (etapa atual)

Os parsers preservam as declarações dos objetos no segundo operando de
`OMP_TRANSACTION`; nomes que não designam variáveis/parâmetros são rejeitados.
A instanciação C++ substitui essa lista. Na gimplificação, a lista é convertida
em chamadas `GOMP_transaction_begin`, `GOMP_transaction_register(address, size)`
e `GOMP_transaction_end`; a saída usa `GIMPLE_TRY_FINALLY`. Referências C++
registram o objeto referido. Exigem-se tipos completos de tamanho constante.
As chamadas comuns passam pelo remapeamento de dados OpenMP existente, e a
lista não precisa sobreviver no marcador GIMPLE.

A ponte provisória na libgomp chama `newtm_hash_create` uma vez, no primeiro
uso, `newtm_hash_acquire` para cada objeto distinto no contexto, e
`newtm_hash_release` somente ao sair do nível externo. Aninhamento por chamada
de função também compartilha esse contexto. No encerramento normal da
biblioteca, chama `newtm_hash_destroy` se houve uso. As outras primitivas não
são chamadas artificialmente: ainda não há operações de acesso transacional,
metadados ou diagnóstico de contagem que as exijam.

As primitivas apenas imprimem seus argumentos. Os antigos `gomp_fatal` foram
substituídos por saídas definidas: estados INVALID, handles nulos e consultas
nulas/zero. A ponte ignora esses estados **apenas nesta etapa de instrumentação**;
não há tabela ou entradas reais, e os logs mostram handles nulos. Uma lista
mínima por execução controla as mensagens de aquisição/liberação e duplicatas;
não armazena valores dos objetos nem implementa hash, reservas ou conflitos.
O log consiste exclusivamente nos `printf` existentes, sem outro mecanismo.

O contexto acompanha execução síncrona na thread. Tasks com suspensão/migração
e dispositivos não estão suportados. O corpo continua acessando memória comum;
a saída excepcional apenas limpa o contexto, sem commit, abort ou rollback.
Os relatos abaixo são históricos e não descrevem integralmente esta etapa.

Validação desta etapa:

- Build GCC concluído com `make -C /usr/src/gcc-build/gcc -j6 all`, seguido
  de checagem incremental; libgomp reconstruída com `make -j3 all`.
- Novos testes `transaction-log-*`: C, 8 PASS; C++, 33 PASS; libgomp,
  6 PASS (compilação, execução e padrão de saída em C e C++).
- Regressão `critical-*`: C, 20 PASS; C++, 60 PASS.
- Conjunto `transaction-*`: C, 46 PASS/7 FAIL; C++, 165 PASS/21 FAIL.
  Sem UNRESOLVED nos resultados finais. Todas as linhas excedentes que
  causaram FAIL são mensagens `[newtm]` de depuração já presentes nos parsers
  e em omplower; não foram filtradas dos testes históricos para esconder falhas.
- Nove execuções adicionais passaram: três programas C/C++ em O0, O2 e
  O2+LTO, cobrindo quatro threads, duplicatas, aninhamento léxico/por função,
  tipos e tamanhos, referências, lambdas e cleanup durante exceção. Um teste
  adicional confirmou endereços das cópias private/reduction; outro confirmou
  exportação/importação e instanciação de template em módulo C++.
- Testes diretos da ponte confirmaram liberação somente na saída externa e
  80 pares acquire/release sob quatro threads, com uma criação/destruição global.

Os testes de execução precisam da libgomp reconstruída. Na regressão GCC,
foi necessário definir GCC_UNDER_TEST/GXX_UNDER_TEST com `-L` e rpath para
`/usr/src/gcc-build/x86_64-pc-linux-gnu/libgomp/.libs`: o driver padrão estava
ligando a versão antiga e falhava com símbolos GOMP_transaction_* ausentes.
Não foi feita instalação do compilador ou runtime, nem executada a suíte completa.

Evidências: `/tmp/newtm-calls-build-final.log`,
`/tmp/newtm-calls-build-check.log`, `/tmp/newtm-calls-lambda-build.log`,
`/tmp/newtm-calls-runtime-final.log`, `/tmp/newtm-calls-verify.py`,
`/tmp/newtm-calls-regression.py`, `/tmp/newtm-calls-module/` e os resumos/logs
nos diretórios `testsuite-newtm-calls`, `testsuite-newtm-calls-all`
(critical) e `testsuite-newtm-calls-transaction` sob o build GCC. Os resultados
da libgomp estão em seu diretório `testsuite/libgomp.sum` e `.log`.

# Implementação de `#pragma omp transaction`

## Depuração de identificadores e tipos

Os parsers C e C++ agora consultam `lookup_name` durante a análise de cada
identificador, antes de entrar no corpo, e escrevem em `stderr`:

```text
[newtm] transaction: contador: int
[newtm] transaction: valor: double
[newtm] transaction: ausente: undef
```

A saída é incondicional para cada argumento, inclusive repetições. Não é
emitida uma linha para a forma sem argumentos. A ausência de declaração
visível imprime `undef`, sem emitir erro de nome não declarado. A consulta é
apenas para depuração: não cria uso avaliado, captura ou armazenamento na IR.
Os tipos são obtidos de `TREE_TYPE` de declarações válidas; C usa
`print_generic_expr` e C++ usa `type_as_string`, preservando referências e
tipos dependentes. Typedefs podem aparecer com seu nome de alias.

Em C++, a consulta usa os bindings ativos diretamente, evitando os diagnósticos
e efeitos do wrapper do parser para esta instrumentação. Ela não constitui uma
validação completa de acessibilidade. Sobrecargas sem tipo único aparecem como
`<overload>`; entidades encontradas sem tipo utilizável aparecem como
`<no type>`. Um namespace não tem tipo de objeto, mesmo que o nó interno
contenha `void_type_node`. Tipos dependentes são mostrados na definição do
template, por exemplo `T&`; não há nova mensagem na instanciação.

Validação: rebuild incremental em `/usr/src/gcc-build/gcc`; verificações
diretas de C/C++ confirmaram parâmetros, qualificadores, ponteiros, arrays,
structs, aliases, ocultação, repetições, nomes ausentes, escopos encerrados,
declarações posteriores e locais exclusivos do corpo. Também foram verificados
referências, membros e templates C++, além da recuperação após lista vazia.

Execução de `gomp.exp=transaction-*`, com
`TESTSUITEDIR=testsuite-newtm-scope-debug`: C, 36 PASS/7 FAIL; C++,
126 PASS/21 FAIL. As falhas são verificações de saída adicional: incluem as
mensagens preexistentes de `omp-low.cc` e a saída de depuração solicitada nos
testes `transaction-4.c` e `transaction-10.c`. Os testes de execução passaram.
Não foram modificadas as expectativas da suíte para ocultar essas mensagens.
Evidências: `/tmp/newtm-scope-debug/`,
`/tmp/newtm-scope-debug-build.log` e os `.sum`/`.log` no diretório de testes
acima. A suíte completa não foi executada.

## Resultado

A extensão está implementada em C e C++ nesta árvore GCC 16.2.0. Aceita
`#pragma omp transaction` seguido de um bloco estruturado, sem cláusulas. A sintaxe também aceita uma
lista opcional não vazia de identificadores separados por vírgula entre
parênteses: `#pragma omp transaction(a, b, c)`. Os nomes não são resolvidos
como variáveis, não são armazenados na representação intermediária e ainda
não têm efeito semântico.
O corpo executa normalmente para cada thread que encontrar a região, usando
o ambiente de dados da construção OpenMP externa. Regiões aninhadas são aceitas.

Foram consultados `OPENMP_AGENT.md` e `OPENMP_CRITICAL_AGENT.md`, e o percurso
foi conferido diretamente nos fontes. Esses dois documentos não foram alterados.

## Representação e processamento

```text
PRAGMA_OMP_TRANSACTION
  -> newtm_c_parser_transaction / newtm_cp_parser_transaction
  -> newtm_finish_transaction
OMP_TRANSACTION (um operando: corpo)
  -> gimplify_expr: caso OMP_TRANSACTION
  -> newtm_build_transaction
GIMPLE_OMP_TRANSACTION (layout GSS_OMP)
  -> verificações de bloco estruturado
  -> varredura OpenMP
  -> newtm_lower_transaction, no passe omplower
sequência GIMPLE comum, preservando binds e cleanups do corpo
  -> passes de controle, exceções, CFG, otimização e geração de código
```

O construtor GIMPLE está em `gcc/gimple.cc`, declarado em `gcc/gimple.h`.
A conversão é chamada pelo dispatcher de `gcc/gimplify.cc`.
O lowering específico está em `gcc/omp-low.cc`: transforma recursivamente o
corpo e substitui a instrução por essa sequência com `gsi_replace_with_seq`.

O código `GIMPLE_OMP_TRANSACTION` permanece explícito até esse lowering.
Não é convertido em `GIMPLE_OMP_CRITICAL`, `GIMPLE_TRANSACTION` ou outro alias.
Não sobrevive até `ompexp`, portanto não exige um caso novo em `omp-expand.cc`.

O passe de varredura registra um contexto para permitir o lowering de regiões
isoladas, mas percorre o corpo com o contexto externo. O lowering também usa
esse contexto externo. Assim, a extensão não oculta regras de aninhamento nem
o tratamento das variáveis da construção OpenMP envolvente.

## Funções novas e componentes reutilizados

As cinco funções específicas usam o prefixo exigido:

| Função | Arquivo | Responsabilidade |
| --- | --- | --- |
| `newtm_c_parser_transaction` | `gcc/c/c-parser.cc` | Parser C; rejeita tokens adicionais na diretiva e analisa o corpo. |
| `newtm_cp_parser_transaction` | `gcc/cp/parser.cc` | Parser C++; preserva a localização e analisa o corpo. |
| `newtm_finish_transaction` | `gcc/c-family/c-omp.cc` | Finalizador compartilhado que constrói `OMP_TRANSACTION`. |
| `newtm_build_transaction` | `gcc/gimple.cc` | Aloca `GIMPLE_OMP_TRANSACTION` e define seu corpo. |
| `newtm_lower_transaction` | `gcc/omp-low.cc` | Substitui a região pelo corpo sequencial transformado. |

Não foram criados tipos auxiliares, estruturas ou variáveis globais específicas.
A representação reutiliza `gimple_statement_omp`, pelo layout `GSS_OMP`.
Os identificadores novos foram pesquisados antes da implementação; não foram
encontrados conflitos com `newtm_` ou `OMP_TRANSACTION` nos fontes examinados.

Foram reutilizados o registro adiado de pragmas, os dispatchers OpenMP,
os parsers de blocos estruturados, `build1_loc`, `add_stmt`, `gimple_alloc`,
`gimple_omp_set_body`, os acessores genéricos de corpo, a cópia de sequências,
os walkers OpenMP, a impressão de regiões com apenas corpo e as verificações
existentes de blocos estruturados e aninhamento.

## Arquivos alterados

| Grupo | Arquivos |
| --- | --- |
| Registro e classificação | `gcc/c-family/c-pragma.h`, `gcc/c-family/c-pragma.cc`, `gcc/c-family/c-common.h`, `gcc/c-family/c-omp.cc` |
| Parsing | `gcc/c/c-parser.cc`, `gcc/cp/parser.cc` |
| GENERIC | `gcc/tree.def`, `gcc/tree.h`, `gcc/tree-pretty-print.cc` |
| GIMPLE | `gcc/gimple.def`, `gcc/gimple.h`, `gcc/gimple.cc`, `gcc/gimple-walk.cc`, `gcc/gimple-pretty-print.cc` |
| Conversão e lowering | `gcc/gimplify.cc`, `gcc/omp-low.cc`, `gcc/gimple-low.cc` |
| Cópia e funções aninhadas | `gcc/tree-inline.cc`, `gcc/tree-nested.cc` |
| Templates, módulos e classificação C++ | `gcc/cp/pt.cc`, `gcc/cp/module.cc`, `gcc/cp/constexpr.cc` |
| Manual | `gcc/doc/extend.texi` |

São 22 arquivos de implementação e um arquivo do manual. Foram adicionados
dez testes e este relatório. Não foram alterados fontes ou testes da `libgomp`
nem a infraestrutura de GNU Transactional Memory.

`CASE_GIMPLE_OMP`, em `gimple.h`, inclui o código novo; seus consumidores
genéricos, como o verificador de `tree-cfg.cc`, passam a reconhecê-lo sem
alterações locais. `gimple-low.cc` verifica a invariável de que a região já
deve ter sido eliminada em `omplower`. Os streamers de GIMPLE/LTO foram
examinados; a região é removida antes da serialização normal. Módulos C++
precisam reconhecer a árvore que pode permanecer em uma definição de template,
por isso `cp/module.cc` recebeu sua classificação explícita como OpenMP.

## Testes adicionados

| Arquivo | Cobertura |
| --- | --- |
| `gcc/testsuite/c-c++-common/gomp/transaction-1.c` | Execução, região vazia, declarações, atribuições, condição, laço, aninhamento e dump legível antes/depois do lowering. |
| `gcc/testsuite/c-c++-common/gomp/transaction-2.c` | Dump bruto com `gimple_omp_transaction`; ausência de alias e de chamadas GOMP/ITM; assembly sem essas chamadas. |
| `gcc/testsuite/c-c++-common/gomp/transaction-3.c` | Execução em `parallel`, redução externa, regiões aninhadas e worksharing dentro da região. |
| `gcc/testsuite/c-c++-common/gomp/transaction-4.c` | Rejeição de cláusulas e nome, com recuperação para a construção seguinte. |
| `gcc/testsuite/c-c++-common/gomp/transaction-5.c` | Contexto de arquivo inválido e corpo ausente. |
| `gcc/testsuite/c-c++-common/gomp/transaction-6.c` | Rejeição de saltos que entram ou saem da região. |
| `gcc/testsuite/c-c++-common/gomp/transaction-7.c` | Preserva a proibição de regiões críticas aninhadas com o mesmo nome, mesmo com transaction intermediária. |
| `gcc/testsuite/c-c++-common/gomp/transaction-8.c` | `_Pragma`, composição com critical e contagem das chamadas de sincronização explicitamente solicitadas. |
| `gcc/testsuite/gcc.dg/gomp/transaction-1.c` | Função aninhada C com acesso a variável externa. |
| `gcc/testsuite/g++.dg/gomp/transaction-1.C` | Template e execução de construtor/destrutor de objeto local. |

## Compilação e resultados

Foi utilizada a árvore existente `/usr/src/gcc-build`, no estágio 3,
configurada para C, C++ e LTO. DejaGNU foi instalado para executar a suíte.
O rebuild foi realizado com:

```sh
make -C /usr/src/gcc-build/gcc -j3 all
```

O comando terminou com sucesso e foi seguido de uma checagem incremental,
também bem-sucedida, para incorporar as últimas edições feitas durante o rebuild.
Houve um aviso de possível variável não inicializada em `gcc/expmed.cc`, arquivo
não alterado. O build informa que os self-tests internos não estão habilitados.
Não foi feita instalação do compilador reconstruído nem um novo bootstrap
completo de três estágios.

Comandos principais dos testes executados:

```sh
make -C /usr/src/gcc-build/gcc check-gcc TESTSUITEDIR=testsuite-newtm RUNTESTFLAGS='gomp.exp=transaction-*'
make -C /usr/src/gcc-build/gcc check-g++ TESTSUITEDIR=testsuite-newtm RUNTESTFLAGS='gomp.exp=transaction-*'
make -C /usr/src/gcc-build/gcc check-gcc TESTSUITEDIR=testsuite-newtm-critical RUNTESTFLAGS='gomp.exp=critical-*'
make -C /usr/src/gcc-build/gcc check-g++ TESTSUITEDIR=testsuite-newtm-critical RUNTESTFLAGS='gomp.exp=critical-*'
make -C /usr/src/gcc-build/x86_64-pc-linux-gnu/libgomp check RUNTESTFLAGS='c.exp=critical-*.c c++.exp=critical-*.c'
```

| Conjunto | Resultado |
| --- | --- |
| Novos testes, C | 33 PASS |
| Novos testes, C++ | 117 PASS, com variações de padrão de linguagem executadas pelo driver |
| Regressão critical, C | 20 PASS; mesmos resultados da referência anterior ao rebuild |
| Regressão critical, C++ | 60 PASS; mesmos resultados da referência anterior ao rebuild |
| Regressão critical, libgomp | 28 PASS; mesmos resultados da referência anterior ao rebuild |
| Execuções adicionais | 12 PASS: C/C++, O0/O2/O2+LTO, uma/quatro threads |
| Módulo C++ | 1 PASS: exportação/importação e instanciação de template contendo transaction |

Os resumos desses subconjuntos não contêm FAIL, UNRESOLVED ou UNSUPPORTED.
Os testes de execução foram efetivamente executados, conforme registrado nos
arquivos `.sum` e `.log`. A suíte completa do GCC/OpenMP não foi executada.

Resultados e evidências locais:

- `/tmp/newtm-build.log` e `/tmp/newtm-build-final.log`: rebuild e checagem incremental.
- `/usr/src/gcc-build/gcc/testsuite-newtm/gcc/gcc.sum` e `gcc.log`.
- `/usr/src/gcc-build/gcc/testsuite-newtm/g++/g++.sum` e `g++.log`.
- `/usr/src/gcc-build/gcc/testsuite-newtm-critical/`: resumos/logs de critical.
- `/usr/src/gcc-build/x86_64-pc-linux-gnu/libgomp/testsuite/libgomp.sum` e `libgomp.log`.
- `/tmp/newtm-critical-baseline-c.sum`, `/tmp/newtm-critical-baseline-cxx.sum`
  e `/tmp/newtm-critical-runtime-baseline.sum`: referência anterior ao rebuild.
- `/tmp/newtm-integration-checks/results.json`: verificações adicionais.
- `/tmp/newtm-implementation.patch`: patch de revisão dos fontes/manual/testes.

A inspeção direta produziu, antes do lowering:

```text
gimple_omp_transaction <
  BODY <
    gimple_assign <mem_ref, _1, *value, NULL, NULL>
    gimple_assign <plus_expr, _2, _1, 1, NULL>
    gimple_assign <ssa_name, *value, _2, NULL, NULL>
  > >
```

Depois de `omplower`, permaneceu somente:

```text
_1 = *value;
_2 = _1 + 1;
*value = _2;
```

Os dumps e assembly dessa inspeção estão em
`/tmp/newtm-representation.c.007t.gimple`,
`/tmp/newtm-representation.c.010t.omplower` e
`/tmp/newtm-representation.s`. Nenhum deles contém chamadas GOMP/ITM.

## Limitações e extensão futura

A diretiva é experimental e não pertence ao padrão OpenMP. Não oferece
atomicidade, isolamento, rollback, sincronização ou operações de memória
transacional. Não aceita cláusulas; aceita a lista opcional de identificadores descrita acima. É registrada por `-fopenmp`,
não por `-fopenmp-simd` isoladamente. A entrada foi implementada apenas em C e
C++; não foi adicionada sintaxe Fortran. Permanecem as restrições da
infraestrutura OpenMP, incluindo blocos estruturados, contextos de aninhamento
e rejeição em funções constexpr C++.

A validação foi feita em x86_64 GNU/Linux. Nenhum dispositivo de offload foi
validado, e não há promessa de identidade transacional entre host/dispositivo.
Com `-fopenmp`, o driver continua usando as regras usuais de ligação OpenMP;
a região transaction em si não adiciona chamadas ao runtime.

Para uma semântica transacional futura, os pontos explícitos de extensão são
`OMP_TRANSACTION`, `GIMPLE_OMP_TRANSACTION`, seu construtor e
`newtm_lower_transaction`. Campos, cláusulas, validações e mecanismos de
execução futuros devem ser acrescentados somente após definir essa semântica.
O código atual não escolhe locks, GNU TM ou qualquer runtime transacional como
implementação futura implícita.

## Extensão sintática: lista de identificadores

Os parsers C e C++ aceitam a forma sem argumentos e a forma
`transaction (identificador [, identificador] ...)`. Listas vazias,
expressões, separadores ausentes e vírgulas finais são rejeitados, com
recuperação até o fim do pragma e análise normal do corpo seguinte.
Os identificadores podem ser repetidos e não precisam estar declarados.
Esta alteração é apenas sintática: os operandos de GENERIC/GIMPLE e o
lowering permanecem os mesmos.

O teste `transaction-4.c` agora aceita `(named)`. Foram acrescentados
`transaction-9.c` (execução, lista simples/múltipla, macros, `_Pragma`,
aninhamento e compatibilidade com a forma original) e `transaction-10.c`
(diagnósticos e recuperação de erros), ambos compartilhados por C e C++.

Validação desta extensão: rebuild com `make -C /usr/src/gcc-build/gcc -j3 all`
concluído com sucesso. Executados `check-gcc` e `check-g++`, com
`TESTSUITEDIR=testsuite-newtm-identifiers` e `RUNTESTFLAGS='gomp.exp=transaction-*'`.
Resultados: C, 38 PASS e 5 FAIL; C++, 132 PASS e 15 FAIL. Todas as verificações
dos testes novos e do `transaction-4.c` atualizado passaram, incluindo execução
em C e C++ (gnu++98, gnu++20 e gnu++26). As falhas restantes são exclusivamente
saída inesperada das mensagens `[newtm] Inicio/Fim do tratamento das variaveis
da transacao`, provenientes de alterações já existentes em `gcc/omp-low.cc`,
preservadas nesta tarefa. O teste novo de execução filtra essas mensagens.
Não houve ERROR, UNRESOLVED ou UNSUPPORTED nos resumos finais.
Logs de build/testes: `/tmp/newtm-identifiers-build.log`,
`/tmp/newtm-identifiers-test-c.log` e `/tmp/newtm-identifiers-test-cxx.log`.
