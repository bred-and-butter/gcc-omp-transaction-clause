# Agente orientador: escopo e tipos dos identificadores de transaction

## Missão e limites

Oriente a implementação da resolução dos identificadores de
`#pragma omp transaction(a, b)` nesta árvore GCC 16.2.0, para verificar sua
visibilidade no ponto da diretiva e obter seus tipos. Leia também
[OPENMP_AGENT.md](OPENMP_AGENT.md),
[OPENMP_CRITICAL_AGENT.md](OPENMP_CRITICAL_AGENT.md) e
[OPENMP_TRANSACTION_IMPLEMENTATION.md](OPENMP_TRANSACTION_IMPLEMENTATION.md).

Este documento foi elaborado por inspeção dos fontes locais. Os caminhos são
relativos à raiz dos fontes `/usr/src/gcc-16.2.0`; o build fica em
`/usr/src/gcc-build`. Localize funções pelo nome e confira seus chamadores.
A criação deste guia não implementa a validação no compilador nem altera a
semântica da diretiva. Não acrescente sincronização ou memória transacional
como consequência da resolução dos nomes.

## Estado encontrado

- `newtm_c_parser_transaction`, em `gcc/c/c-parser.cc`, consome cada
  `CPP_NAME` com `c_parser_require` e descarta seu valor.
- `newtm_cp_parser_transaction`, em `gcc/cp/parser.cc`, chama
  `cp_parser_identifier` e descarta o identificador retornado.
- Ambos analisam o corpo depois do fim do pragma e chamam
  `newtm_finish_transaction(loc, body)`, em `gcc/c-family/c-omp.cc`.
- `OMP_TRANSACTION` possui somente o corpo. O construtor
  `newtm_build_transaction` e o lowering também não recebem identificadores.
- Em `gcc/cp/pt.cc`, o caso `OMP_TRANSACTION` compartilha o tratamento de
  construções que só instanciam o corpo; não substitui uma lista de argumentos.

Aceitar um token identificador não resolve seu nome. O `IDENTIFIER_NODE`
representa a grafia; não é a declaração e não fornece o tipo do objeto.
O pré-processador não determina a visibilidade nem o tipo C/C++.

## Momento correto e contrato semântico

Resolva cada nome durante a análise da lista, antes de chamar
`c_parser_omp_structured_block` ou `cp_parser_omp_structured_block`. Nesse ponto,
os bindings ativos correspondem ao contexto da diretiva. Uma variável declarada
somente dentro do corpo não pode tornar válido um argumento da diretiva.
Uma declaração posterior no bloco também não deve ser encontrada por uma
busca adiada. Respeite as regras próprias de C++ para contexto de classe e
templates, em vez de simular visibilidade por ordem textual.

Use a busca da linguagem: ela trata escopos externos e ocultação por declarações
mais internas. Não procure identificadores por texto nos fontes, não percorra
apenas os locais da função e não use `DECL_CONTEXT` como teste de visibilidade.
Um nome global pode estar visível dentro de uma função, e o contexto de uma
declaração não informa sozinho qual binding está ativo.

Antes de implementar rejeições, defina quais entidades transaction admite.
O pedido de conhecer tipos não determina se funções, constantes de enumeração,
typedefs ou membros são argumentos válidos. Separe três resultados:

1. O nome foi encontrado segundo as regras da linguagem?
2. A entidade encontrada pertence a uma categoria permitida pela extensão?
3. Seu tipo já é conhecido ou depende de instanciação?

Se o contrato escolhido for **variáveis e parâmetros**, aceite inicialmente
`VAR_DECL` e `PARM_DECL`; documente essa escolha. Não rejeite parâmetros por
testar apenas `VAR_DECL`. Defina separadamente membros não estáticos
(`FIELD_DECL`), structured bindings, constantes e outros casos C++.
Preserve a gramática atual de identificadores simples: usar um parser de
expressões ou listas OpenMP mais amplo não deve habilitar silenciosamente
`ns::x`, `obj.x`, `*p`, seções de arrays ou expressões.

## Resolução no front-end C

Referências concretas:

- `gcc/c/c-decl.cc`: `lookup_name(tree name)` consulta o binding ordinário
  atual (`I_SYMBOL_BINDING`), considera sua visibilidade e retorna a declaração
  ou `NULL_TREE`. Leia as exceções existentes, inclusive versões de funções.
- `gcc/c/c-parser.cc`: no processamento de listas OpenMP, o trecho que chama
  `lookup_name(c_parser_peek_token(parser)->value)` mostra o diagnóstico via
  `undeclared_variable` e a recuperação com `error_mark_node`.
- `gcc/c/c-parser.cc`: `c_parser_omp_var_list_parens` é uma entrada para
  estudar esse fluxo; suas regras completas não são a gramática de transaction.

Em `newtm_c_parser_transaction`, capture `value` e `location` do token antes
da chamada que o consome. Depois de confirmar `CPP_NAME`, use o identificador
em `lookup_name`. Para ausência, reutilize `undeclared_variable(loc, id)`
conforme o padrão local e marque o item como inválido. Para uma declaração
encontrada, valide sua categoria antes de extrair o tipo.

Pseudocódigo, a adaptar à recuperação existente:

```text
confirmar token CPP_NAME
salvar id = token->value e loc = token->location
consumir identificador
decl = lookup_name(id)
se decl é nulo: diagnosticar nome não declarado
senão se decl é error_mark_node: propagar falha sem diagnóstico duplicado
senão: classificar entidade e obter TREE_TYPE(decl), quando aplicável
continuar vírgula / fechamento / fim do pragma
```

O namespace ordinário de C inclui variáveis, funções e typedefs; encontrar um
nome não prova que é variável. Um tag `struct S` pertence a outro namespace e
não deve ser buscado como variável. Confira também `id_kind`: o parser atual
aceita `CPP_NAME` sem restringir a `C_ID_ID`, e um typedef pode chegar como nome
com outra classificação. Não transforme uma restrição de categoria em uma
mensagem enganosa de identificador inexistente.

## Resolução no front-end C++

Referências concretas em `gcc/cp/parser.cc`:

- `cp_parser_identifier`: análise do identificador simples, sem resolver nome.
- `cp_parser_lookup_name_simple(parser, name, location)`: wrapper local de
  `cp_parser_lookup_name` com checagem de dependência e busca usual de nome
  que não foi solicitado como tipo, template ou namespace.
- `cp_parser_lookup_name`: busca conforme o estado do parser, tratamento de
  ambiguidades e checagem de acessibilidade. Seu resultado não é sempre uma
  declaração simples; pode incluir sobrecargas e representações dependentes.
- `cp_parser_omp_var_list_no_open`: exemplo de resolução com o wrapper,
  diagnóstico por `cp_parser_name_lookup_error`, e tratamento de referências
  a variáveis automáticas externas por `outer_automatic_var_p` e
  `process_outer_var_ref`.
- `gcc/cp/name-lookup.cc`: implementação de `lookup_name` e infraestrutura
  de busca C++. Não confunda sua interface com a função homônima de C.

Em `newtm_cp_parser_transaction`, preserve a localização antes de
`cp_parser_identifier`, guarde seu retorno e resolva o nome com o mecanismo
do parser. O padrão de `cp_parser_omp_var_list_no_open` é o ponto de partida;
não substitua toda a rotina pela lista OpenMP existente, que aceita sintaxe e
aplica tratamentos específicos de cláusulas diferentes.

Trate `error_mark_node` da análise e da resolução. Siga o padrão de diagnóstico
do chamador e confira se a busca já emitiu erro, evitando mensagens duplicadas.
Não aplique `TREE_TYPE` indiscriminadamente a `OVERLOAD`, `BASELINK`,
`SCOPE_REF` ou qualquer resultado ainda não classificado.

Encontrar uma declaração e ter acesso permitido são condições distintas. A
infraestrutura de busca pode emitir um erro de acesso e ainda retornar uma
declaração; um retorno diferente de `error_mark_node` não certifica sozinho
que toda a validação semântica passou.

Para lambdas, funções locais e membros, defina se listar um nome conta como
uso ou captura. O processamento de variáveis externas das cláusulas OpenMP é
uma referência a estudar, não uma operação a copiar sem esse contrato.
Obter metadados de tipo não deve causar inadvertidamente uma avaliação do
objeto ou uma conversão lvalue-to-rvalue.

## Como obter e interpretar o tipo

Após uma resolução válida e classificação da entidade, o tipo declarado de
uma variável ou parâmetro está em `TREE_TYPE(decl)`, definido em `gcc/tree.h`.
Verifique valores nulos e `error_mark_node` antes de usar predicados de tipo.

| Informação | Ponto de partida |
| --- | --- |
| Categoria da entidade | `TREE_CODE(decl)`, `DECL_P(decl)` |
| Tipo declarado da variável/parâmetro | `TREE_TYPE(decl)` |
| Categoria do tipo | `TREE_CODE(type)` e predicados pertinentes de `tree.h` |
| Qualificadores | `TYPE_QUALS(type)` |
| Tipo apontado, referenciado ou elemento de array | `TREE_TYPE(type)`, depois de verificar a categoria |
| Limites do array | `TYPE_DOMAIN(type)` quando o tipo é `ARRAY_TYPE` |
| Dependência de tipo C++ | `dependent_type_p(type)`, declarado em `gcc/cp/cp-tree.h` |
| Impressão em diagnóstico | formato `%qT` com um nó de tipo válido |

Preserve o tipo original. Não aplique `TYPE_MAIN_VARIANT` antes de registrar
qualificadores, pois ele remove diferenças que podem ser relevantes.
Uma referência C++ tem tipo declarado diferente do tipo da expressão após
`convert_from_reference`. Arrays não devem sofrer decay só para consultar seu
tipo. Um parâmetro declarado com sintaxe de array já pode ter sido ajustado
para ponteiro pelas regras da linguagem.

Tipo conhecido não significa tipo completo ou tamanho constante. Ponteiros
para tipos incompletos, arrays de tamanho variável e tipos dependentes exigem
tratamento próprio. Não use `TYPE_SIZE_UNIT` como teste de existência do tipo
e não converta seu valor para inteiro sem validar que é constante e cabe no
formato de destino. Não force layout de tipos para a simples consulta.

## Templates e preservação da informação

Um parâmetro `T &x` de template pode estar perfeitamente visível mesmo quando
seu tipo ainda depende de `T`. Não emita “não declarado” só por não conhecer o
tipo concreto. Faça imediatamente as verificações independentes e adie as
restrições que precisam da substituição.

Se a informação for necessária somente para diagnóstico imediato de entidades
não dependentes, a consulta pode ficar no front-end. Se for necessária na
instanciação ou no lowering, preserve declarações ou representações
dependentes e localizações na árvore da construção; strings isoladas não
preservam identidade nem bindings.

Nesse segundo caso, será necessário planejar alterações de representação:

1. Acrescentar armazenamento e acessores a `OMP_TRANSACTION`, atualizar
   `newtm_finish_transaction` e seus chamadores.
2. Em `gcc/cp/pt.cc`, separar o tratamento de transaction quando necessário,
   substituir os novos dados com a infraestrutura de templates e repetir as
   verificações dependentes. O caso atual só substitui o corpo.
3. Se os dados precisarem chegar ao lowering, atualizar também o layout GIMPLE,
   `newtm_build_transaction`, a conversão em `gcc/gimplify.cc` e seus consumidores.
4. Auditar walkers, cópia, dumps, módulos C++ e eventual serialização, conforme
   o tempo de vida dos dados. Não guardar ponteiros para tokens ou vetores
   temporários em nós persistentes; usar armazenamento rastreado pelo GC.

Não volte a buscar uma string de nome no lowering: o escopo léxico original
já pode ter terminado, e a ocultação torna essa busca incorreta. Preserve a
declaração resolvida e faça o remapeamento necessário para templates, cópia
ou outlining. A visibilidade lexical também não determina sozinha qual cópia
de uma variável `private` de uma região OpenMP externa será usada em runtime.

## Recuperação e testes para uma implementação posterior

Preserve a recuperação até `CPP_PRAGMA_EOL` e analise o corpo mesmo após erro
na lista, conforme o fluxo atual. Um argumento inválido não deve impedir a
análise dos seguintes nem corromper a próxima construção. Defina a política
de duplicatas separadamente; hoje nomes repetidos são aceitos.

Adicione casos compartilhados em `gcc/testsuite/c-c++-common/gomp/` e casos
C++ específicos em `gcc/testsuite/g++.dg/gomp/`, incluindo:

- Variáveis globais, locais e parâmetros visíveis; tipo esperado de cada um.
- Ocultação: global `int x` e local `double x`; resolver a declaração local.
- Nome não declarado, local de bloco encerrado, declaração posterior e nome
  declarado somente no corpo de transaction.
- Categorias encontradas mas não permitidas pelo contrato escolhido: typedef,
  função, enumeração e, em C++, namespace ou conjunto de sobrecargas.
- Ponteiros, arrays, qualificadores, structs e, em C++, referências e classes.
- C++: acesso, membros, lambdas, ambiguidades e templates com tipo dependente;
  testar definição e instanciação, inclusive falhas que só surgem nesta última.
- Macros, `_Pragma`, listas com múltiplos itens e recuperação depois de erro.
- Formas alternativas e módulos C++ se o armazenamento novo atingir esses caminhos.

Verifique a identidade da declaração e o tipo com dumps apropriados ou uma
instrumentação temporária controlada. Compilar sem erro, por si só, não prova
que o tipo correto foi selecionado. Evite mensagens incondicionais no compilador,
que já são uma fonte de falhas registrada no relatório de transaction.

Execute rebuild e os subconjuntos C/C++ de transaction no diretório de build,
seguindo os comandos do relatório de implementação. Confira `.sum` e `.log`,
distinga falhas anteriores de novas regressões e relate os testes realmente
executados. Este guia não executou build ou testes do compilador.

## Critério de conclusão da implementação

A resolução usa o escopo do ponto da diretiva, respeita ocultação e regras da
linguagem, distingue ausência de categoria inválida, obtém tipos sem conversões
indevidas e trata dependência C++ no momento correto. Qualquer informação usada
depois do parsing tem representação persistente e seus consumidores atualizados.
Relate o contrato de entidades aceitas, os testes e limitações remanescentes.
