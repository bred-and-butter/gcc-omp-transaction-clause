# Agente orientador: implementação de diretivas OpenMP no GCC

## Missão e escopo

Você é um agente de desenvolvimento trabalhando nos fontes do GCC. Implemente o
tratamento de novas diretivas `#pragma omp` de ponta a ponta: reconhecimento,
análise sintática, validação semântica, representação intermediária, geração de
código e testes. Não considere uma diretiva implementada apenas porque seu
parser aceita a entrada.

Este guia foi elaborado por inspeção da árvore local, cujo `gcc/BASE-VER` contém
`16.2.0`. Os caminhos abaixo são relativos à raiz dos fontes. Localize funções
pelo nome: números de linha mudam. O foco é C e C++; Fortran usa outro front-end
e a sintaxe `!$omp`, exigindo trabalho próprio quando incluído no escopo.

Não é necessário que `#pragma omp` comece na primeira coluna: o reconhecimento
segue as regras de diretivas do pré-processador, inclusive espaços iniciais.
Considere também `_Pragma` e as formas por atributos OpenMP suportadas nesta
árvore. Não implemente reconhecimento com busca textual de linhas.

## Fluxo identificado no código

```text
fonte C/C++: #pragma omp <diretiva> <cláusulas>
  -> init_pragma: registro no namespace omp
  -> libcpp: CPP_PRAGMA + identificador, tokens e CPP_PRAGMA_EOL
  -> parser C ou C++: contexto, diretiva, cláusulas e corpo associado
  -> análise semântica: tipos, escopos, restrições e construção de árvores
  -> gimplificação: árvores OpenMP para GIMPLE e tratamento das variáveis
  -> diagnose_omp_blocks / omplower: restrições e transformação de regiões
  -> ompexp: fluxo de controle, funções auxiliares e chamadas de execução
  -> passes posteriores e geração de código
  -> libgomp, quando a semântica exige suporte de execução
```

Esse é o caminho típico de construções executáveis com região. Diretivas
declarativas podem modificar declarações ou atributos; outras viram chamadas
diretamente. Nem toda diretiva cria um nó `OMP_*`, um código `GIMPLE_OMP_*` ou
uma nova função de runtime.

O diagrama organiza responsabilidades, não chamadas estritamente separadas:
parsing e validação semântica se intercalam. O registro acontece na inicialização
do front-end, antes do processamento das diretivas do arquivo. `_Pragma` entra
pelo pré-processador; atributos OpenMP são reconhecidos pelos parsers e
convertidos em sequências de tokens de pragma para reutilizar a análise.

### 1. Registro e pré-processamento

- `gcc/c-family/c-pragma.h`: `enum pragma_kind`, identificadores
  `PRAGMA_OMP_*` e enumeração das cláusulas `PRAGMA_OMP_CLAUSE_*`.
- `gcc/c-family/c-pragma.cc`: tabelas `omp_pragmas`, `omp_pragmas_simd` e
  função `init_pragma`.
- `libcpp/directives.cc`: `cpp_register_deferred_pragma` registra o pragma
  adiado; o processamento do pragma entrega `CPP_PRAGMA` com seu identificador.
  O conteúdo é analisado posteriormente pelo front-end.

`init_pragma` registra `omp_pragmas` quando `flag_openmp` está ativo e
`omp_pragmas_simd` quando `flag_openmp || flag_openmp_simd`. Os registros
OpenMP permitem expansão de macros nos argumentos e no reconhecimento do nome.
O modo `directives_only` tem tratamento próprio no registro.

Ao acrescentar um identificador, mantenha-o dentro do intervalo
`PRAGMA_OMP__START_` a `PRAGMA_OMP__LAST_`, atualizando os aliases se mudar a
primeira ou a última entrada. Há consumidores que detectam OpenMP por esse
intervalo, inclusive verificações de código interveniente em laços. Preserve
também o limite de armazenamento dos identificadores: o token do front-end C
reserva oito bits, como documentado em `c_register_pragma_1`.

As tabelas registram a primeira palavra: `parallel for simd`, por exemplo,
começa com `PRAGMA_OMP_PARALLEL`; as palavras seguintes são consumidas pelo
parser. Uma nova forma de `target` ou `declare` pode reutilizar um identificador
existente. A presença em `omp_pragmas_simd` não significa que toda a construção
tenha efeito com `-fopenmp-simd`: os parsers também controlam esse comportamento.

### 2. Despacho e análise sintática

| Responsabilidade | C | C++ |
| --- | --- | --- |
| Arquivo principal | `gcc/c/c-parser.cc` | `gcc/cp/parser.cc` |
| Despacho por contexto | `c_parser_pragma` | `cp_parser_pragma` |
| Construções com corpo, em geral | `c_parser_omp_construct` | `cp_parser_omp_construct` |
| Identificação de cláusulas | `c_parser_omp_clause_name` | `cp_parser_omp_clause_name` |
| Lista e máscara de cláusulas | `c_parser_omp_all_clauses` | `cp_parser_omp_all_clauses` |
| Exemplo de região paralela | `c_parser_omp_parallel` | `cp_parser_omp_parallel` |

O despacho distingue contextos como nível de arquivo, bloco e posição de
instrução. Diretivas independentes, como `barrier`, têm caminhos específicos;
não force todas as diretivas pelo dispatcher de construções com corpo.

Uma rotina de parser deve consumir a sintaxe da diretiva até o fim do pragma,
validar as cláusulas permitidas por sua máscara, analisar o corpo associado
quando existir e preservar a localização para diagnósticos. Use os mecanismos
existentes de recuperação até `CPP_PRAGMA_EOL`; uma entrada inválida não deve
corromper a análise das próximas instruções.

As máscaras `OMP_*_CLAUSE_MASK` são definidas nos respectivos arquivos de parser.
O tipo compartilhado `omp_clause_mask` e `OMP_CLAUSE_MASK_1` estão em
`gcc/c-family/c-common.h`. Alterar somente a máscara de C não habilita a cláusula
em C++; altere e valide os dois caminhos.

### 3. Semântica, classificação e cláusulas

- `gcc/c/c-typeck.cc`: `c_finish_omp_clauses`, `c_begin_omp_parallel`,
  `c_finish_omp_parallel` e outros finalizadores C.
- `gcc/cp/semantics.cc`: `finish_omp_clauses`, `finish_omp_parallel` e
  finalizadores C++; tratam particularidades de tipos e objetos C++.
- `gcc/cp/pt.cc`: substituição e instanciação de templates, incluindo
  `tsubst_omp_clauses` e tratamento de árvores OpenMP.
- `gcc/c-family/c-omp.cc`: utilitários compartilhados, finalizadores comuns,
  `c_omp_split_clauses` e tabela `c_omp_directives`.
- `gcc/c-family/c-common.h`: interfaces compartilhadas e classificações.

`c_omp_directives` descreve diretivas individuais, inclusive nomes de até três
palavras, identificador, categoria e o campo booleano `simd`. Não é uma lista
de todas as construções combinadas: `c_omp_categorize_directive` identifica a
diretiva inicial e os parsers analisam a combinação restante. As categorias
incluem declarativa, informativa, independente, construção, utilitária e meta.
Sua ordenação é
explícita: ordem alfabética pela primeira palavra; entradas com segunda/terceira
palavra precedem as entradas correspondentes com valores nulos. Revise essa
tabela e seus consumidores para atributos, metadiretivas e cláusulas
`absent`/`contains` quando adicionar uma forma de diretiva. Defina `simd`
coerentemente com a semântica pretendida: consumidores de atributos usam esse
campo para decidir o processamento quando `flag_openmp` está desativado. Ele
é distinto da escolha entre as tabelas de registro de pragmas.

Construções combinadas usam máscaras acumuladas e repartição de cláusulas por
`c_omp_split_clauses`, com índices `C_OMP_CLAUSE_SPLIT_*`. Não basta aceitar uma
cláusula: ela precisa alcançar a região correta e manter sua semântica nas
combinações. Preserve as marcações de construções combinadas usadas nas fases
seguintes.

### 4. Árvores, GIMPLE e passes

| Camada | Arquivos e pontos de partida |
| --- | --- |
| Códigos de árvores | `gcc/tree.def` |
| Cláusulas e estruturas de árvores | `gcc/tree-core.h`, `gcc/tree.h`, `gcc/tree.cc` |
| Códigos e estruturas GIMPLE | `gcc/gimple.def`, `gcc/gimple.h`, `gcc/gimple.cc` |
| Gimplificação | `gcc/gimplify.cc`: `gimplify_omp_parallel`, varredura e ajuste de cláusulas |
| Contextos e transformação de regiões | `gcc/omp-low.cc`: `scan_omp_*`, `lower_omp_*`, `execute_lower_omp` |
| Expansão de regiões | `gcc/omp-expand.cc`: `expand_omp`, `expand_omp_taskreg`, `execute_expand_omp` |
| Ordem dos passes | `gcc/passes.def` |
| Seleção de variantes e dispositivos | `gcc/omp-general.cc`, `gcc/omp-offload.cc` |

No exemplo `parallel`, `c_finish_omp_parallel` cria `OMP_PARALLEL` com corpo,
cláusulas e localização. `gimplify_omp_parallel` examina as cláusulas, gimplifica
o corpo, ajusta compartilhamento de dados e cria `GIMPLE_OMP_PARALLEL` por
`gimple_build_omp_parallel`. `scan_omp_parallel` e `lower_omp_taskreg` participam
da preparação de contextos, dados e função auxiliar; `expand_omp_taskreg` e o
código de chamadas paralelas materializam a execução.

Em `gcc/passes.def`, `pass_diagnose_omp_blocks` precede `pass_lower_omp`;
`pass_lower_cf`, o tratamento de exceções e `pass_build_cfg` aparecem antes de
`pass_expand_omp`. A expansão trabalha sobre o grafo de fluxo de controle.
Existe ainda `pass_expand_omp_ssa` (`ompexpssa`) em outros pontos do pipeline;
avalie esse caminho quando a transformação puder deixar ou gerar OpenMP nessa
fase. Não é necessário criar um passe novo para cada diretiva.

As restrições não se concentram nos finalizadores: em `gcc/omp-low.cc`,
`check_omp_nesting_restrictions` é chamada durante a varredura das instruções
OpenMP; `diagnose_omp_structured_block_errors` verifica transferências de
controle ilegais. Inclua a nova construção nas verificações pertinentes, além
de implementar sua transformação.

Ao criar uma representação nova, audite também impressão, walkers, cópia,
operandos, verificação e serialização/LTO. Pontos usuais incluem
`gcc/tree-pretty-print.cc`, `gcc/gimple-pretty-print.cc`, `gcc/tree-inline.cc`,
`gcc/tree-cfg.cc` e `gcc/gimple-walk.cc`; localize os casos da representação
análoga em toda a árvore para decidir quais realmente precisam mudar.
Para LTO, examine também `gcc/tree-streamer-in.cc`, `gcc/tree-streamer-out.cc`,
`gcc/gimple-streamer-in.cc` e `gcc/gimple-streamer-out.cc` conforme os dados
que sobrevivem até a serialização. Não presuma que toda árvore do front-end
precise de serialização própria: verifique quando ela é eliminada ou convertida.

Novas cláusulas exigem coerência entre a enumeração em `tree-core.h`, acessores
em `tree.h` e tabelas `omp_clause_num_ops`/`omp_clause_code_name` em `tree.cc`,
além dos consumidores pertinentes. Não suponha a existência de um arquivo
`omp-clause.def`: esta árvore mantém essas definições nos arquivos mencionados.

### 5. Runtime e exemplo de caminho curto

`gcc/omp-builtins.def` declara os builtins usados para chamadas `GOMP_*`.
Uma região paralela comum pode usar `GOMP_parallel`, implementada em
`libgomp/parallel.c` e exportada em `libgomp/libgomp.map`. Combinações e outras
semânticas podem selecionar funções diferentes.

Como contraste entre caminhos reais, `c_finish_omp_barrier`, em
`gcc/c-family/c-omp.cc`, cria diretamente uma chamada ao builtin
`BUILT_IN_GOMP_BARRIER`. Portanto, não use `parallel` como modelo universal.

Se precisar de API interna nova, examine `libgomp/libgomp_g.h`,
`libgomp/libgomp.h`, a implementação pertinente e o mapa de símbolos. Preserve
ABI, assinaturas e regras de versionamento existentes. Offload exige avaliação
própria das bibliotecas de dispositivo, plugins e mapeamento de dados; executar
corretamente no host não prova suporte no dispositivo.

## Procedimento obrigatório para uma nova diretiva

1. **Defina a semântica antes da edição.** Registre nome, versão OpenMP ou
   natureza de extensão, gramática, categoria, corpo associado, cláusulas,
   contextos legais, combinações, sincronização e regras de dados. Se depender
   de texto normativo ausente, consulte a especificação oficial pertinente e
   documente a versão e a referência; não infira regras de memória só pelo nome.
2. **Escolha uma diretiva análoga.** Rastreie seu caminho completo. Decida se a
   nova forma reutiliza nós e runtime existentes, altera declarações, vira uma
   chamada ou necessita de uma representação nova.
3. **Registre somente o necessário.** Adicione `PRAGMA_OMP_*` e uma entrada de
   registro se houver uma primeira palavra nova, preservando o intervalo e
   os limites dos identificadores. Uma nova forma de palavra inicial existente
   normalmente exige alteração do seu parser, sem novo registro. Decida explicitamente o
   comportamento com `-fopenmp`, `-fopenmp-simd` e sem essas opções. Atualize a
   classificação compartilhada, incluindo `simd`, respeitando a ordenação.
4. **Implemente C e C++.** Acrescente despacho no contexto correto, parser,
   máscara, diagnóstico e recuperação. Para uma nova cláusula, implemente o
   reconhecimento e análise nos dois front-ends e a representação compartilhada.
5. **Complete a semântica.** Valide duplicatas, exclusões, tipos, escopos,
   referências, nesting e structured blocks conforme aplicável. Verifique
   templates, expressões dependentes, construtores e destrutores em C++.
6. **Garanta o percurso intermediário.** Implemente ou reutilize gimplificação,
   tratamento de variáveis, verificações de aninhamento e blocos estruturados,
   lowering e expansão conforme a representação escolhida. Audite consumidores de novos
   códigos; não deixe nós sem tratamento chegando a passes posteriores.
7. **Complete a execução se necessária.** Reutilize builtins existentes ou
   implemente a interface e runtime novos. Trate cancelamento, exceções,
   barreiras, memória e offload conforme a semântica escolhida.
8. **Teste e documente.** Adicione testes relevantes, execute os subconjuntos
   adequados e descreva limitações. Atualize documentação aplicável, como
   `libgomp/libgomp.texi` e manuais em `gcc/doc`, se o comportamento exigir.

## Validação e critérios de conclusão

Use DejaGNU e os diretórios existentes:

- `gcc/testsuite/c-c++-common/gomp/`: casos compartilhados C/C++.
- `gcc/testsuite/gcc.dg/gomp/`: particularidades de C.
- `gcc/testsuite/g++.dg/gomp/`: C++, especialmente templates e objetos.
- `libgomp/testsuite/libgomp.c-c++-common/`, `libgomp.c/` e `libgomp.c++/`:
  execução e sincronização.

Os drivers `gcc.dg/gomp/gomp.exp` e `g++.dg/gomp/gomp.exp` também executam casos
compartilhados. Siga as convenções locais de `dg-do`, `dg-options`, `dg-error`,
`dg-warning`, `scan-tree-dump` e requisitos de target.

Cubra a sintaxe válida mínima e as cláusulas relevantes; entradas inválidas e
recuperação; contexto e combinações; modo SIMD e opções desabilitadas; macros e
formas alternativas aplicáveis; templates quando pertinentes. Testes de execução
devem verificar resultados, participação de threads e sincronização com
assertivas determinísticas, evitando depender de tempos de execução. Teste LTO
ou dispositivo quando a mudança alcançar essas camadas.

Execute comandos de build e testes no **diretório de construção**, separado
dos fontes. Exemplos, a ajustar à configuração e ao nome dos testes criados:

```sh
make -j4 all-gcc
make -j4 check-gcc RUNTESTFLAGS='gomp.exp=nova-diretiva-*.c'
make -j4 check-g++ RUNTESTFLAGS='gomp.exp=nova-diretiva-*.C'
make -j4 check-g++ RUNTESTFLAGS='gomp.exp=nova-diretiva-*.c'
make -j4 check-target-libgomp RUNTESTFLAGS='c.exp=nova-diretiva-*.c'
make -j4 check-target-libgomp RUNTESTFLAGS='c++.exp=nova-diretiva-*.C'
make -j4 check-target-libgomp RUNTESTFLAGS='c++.exp=nova-diretiva-*.c'
```

Esses exemplos partem da raiz do diretório de construção; use somente os
seletores correspondentes aos arquivos realmente criados. Os comandos C++ com
`.c` selecionam os testes compartilhados, que seriam excluídos por um filtro
restrito a `.C`. Os drivers C/C++ da `libgomp` também incluem seu diretório
compartilhado. Verifique que C e C++ estão habilitados e que DejaGNU está
disponível. O driver C++ da `libgomp` exige a `libstdc++` apropriada ao build;
na sua ausência ele pode deixar de executar os testes.

O build de `all-gcc` sozinho não constrói um runtime modificado; construa também
os componentes de target necessários antes dos testes de execução. Confira os
arquivos `.sum` e `.log`, distinguindo `PASS`, `FAIL`, `UNRESOLVED`, `UNSUPPORTED`
e resultados esperados. Um teste não executado não é evidência de sucesso.

Para rastrear um caso com o compilador **construído a partir desta árvore**, use
as opções `-fopenmp -fdump-tree-original -fdump-tree-gimple
-fdump-tree-omplower -fdump-tree-ompexp`. Para investigar o caminho SIMD,
substitua `-fopenmp` por `-fopenmp-simd`. Inspecione árvores, captura de dados,
função auxiliar e chamadas resultantes; nem toda diretiva produzirá regiões
visíveis em todos os dumps.

A tarefa está concluída quando reconhecimento, semântica e execução prevista
estão coerentes; os testes pertinentes passam; modos alternativos têm
comportamento definido; e o relatório lista arquivos alterados, testes
efetivamente executados e limitações remanescentes. Não apresente aceitação
sintática como suporte completo.

## Buscas úteis para começar

```sh
rg -n 'omp_pragmas|init_pragma' gcc/c-family/c-pragma.cc
rg -n 'PRAGMA_OMP_PARALLEL|c_parser_omp_parallel' gcc/c/c-parser.cc
rg -n 'cp_parser_omp_parallel|cp_parser_omp_construct' gcc/cp/parser.cc
rg -n 'c_omp_directives|c_omp_split_clauses' gcc/c-family/c-omp.cc
rg -n 'OMP_PARALLEL|GIMPLE_OMP_PARALLEL' gcc --glob '*.{cc,h,def}'
rg -n 'GOMP_parallel' gcc/omp-builtins.def gcc/omp-expand.cc libgomp
```

Troque `parallel` e seus identificadores pela diretiva análoga escolhida. Leia
as funções encontradas e seus chamadores antes de editar.
