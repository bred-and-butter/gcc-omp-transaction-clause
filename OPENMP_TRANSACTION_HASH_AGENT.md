## Atualização: chamadas de instrumentação

A diretiva agora gera chamadas à ponte provisória `GOMP_transaction_*`.
Essa ponte chama create/acquire/release/destroy nos pontos de vida útil
correspondentes. As primitivas hash continuam sem funcionalidade: imprimem
argumentos, devolvem INVALID/handles nulos ou null/zero e não encerram mais o
processo. A ponte ignora os estados de falha somente para permitir a sequência
de mensagens nesta etapa. Consulte a seção inicial de
`OPENMP_TRANSACTION_IMPLEMENTATION.md` para o percurso e as limitações atuais.
O restante deste documento preserva a especificação da implementação futura;
os parágrafos históricos sobre `gomp_fatal` não descrevem mais os corpos atuais.

# Agente orientador: biblioteca hash de transaction

## Missão e referências

Oriente a implementação da biblioteca hash usada em runtime pela extensão
`#pragma omp transaction`. Este documento especifica trabalho futuro: sua criação
não autoriza implementar ou modificar o compilador e o runtime nesta etapa.
Leia também `OPENMP_AGENT.md`, `OPENMP_CRITICAL_AGENT.md`,
`OPENMP_TRANSACTION_SCOPE_AGENT.md` e `OPENMP_TRANSACTION_IMPLEMENTATION.md`.
Confira sempre o código: partes desses documentos descrevem estados anteriores.
Os caminhos são relativos à raiz dos fontes; o build existente fica em
`/usr/src/gcc-build`. Localize funções pelo nome, pois linhas mudam.

## Contrato estabelecido e decisões propostas

Estabelecido com o usuário:

- A tabela é global no runtime, compartilhada pelas threads que usam a mesma
  instância da biblioteca. Não é uma tabela de nós internos do compilador.
- A chave é o endereço de memória do objeto listado na diretiva.
- Somente acessos realizados em blocos transaction participam dos conflitos.
  Um acesso fora desses blocos não consulta a tabela, mesmo que outro thread
  tenha o objeto registrado naquele momento.
- A biblioteca deve permitir substituir a política de conflitos.
- Transações aninhadas usam flat nesting: compartilham o contexto e a
  identidade da transação externa, sem confirmação independente.
- A sincronização usa um mutex por bucket. Consultas e atualizações de buckets
  diferentes podem executar em paralelo; operações no mesmo bucket são
  serializadas. Não usar um mutex global no caminho normal dessas operações.
- A configuração inicial usa 1.024 buckets, capacidade configurável,
  colisões por encadeamento e nenhum redimensionamento. O número de buckets
  não limita o número de objetos registrados.

Propostas deste agente para uma primeira implementação, ainda não decisões
expressas do usuário: remover uma entrada quando sua última referência for
liberada. A hash usa igualdade exata da chave canônica; a granularidade por
objeto inteiro foi acordada, mas a hash não resolve sozinha a canonicalização.
Igualdade exata não detecta intervalos parcialmente sobrepostos, como uma
estrutura e um campo situado em endereço diferente. Registrar o tamanho não
resolve essa limitação automaticamente.

A biblioteca mantém registros e referências; a camada transacional aplica a
política inicial explicitada abaixo. Não embutir essa política nas operações
estruturais da hash: futuras políticas devem poder substituir seus metadados e
serviços sem reescrever o encadeamento e a gestão de referências.

## Política transacional inicial acordada

Estas decisões definem o comportamento desejado da camada transacional, não
transformam as primitivas da hash em uma implementação completa de transações.

### Visão integrada e estado da implementação

A política especificada combina detecção pessimista de conflitos antes dos
acessos, reservas compartilhadas/exclusivas mantidas até o término, escritas
adiadas em cópias privadas, abort da transação solicitante e flat nesting.
A lista identifica os objetos elegíveis; a reserva indica a permissão de acesso;
a cópia privada contém os valores pendentes. Esses três conceitos são distintos.

Esta é a política acordada para implementação. Na árvore estudada, os parsers
consultam nomes somente para depuração, as listas não chegam à representação
intermediária e o lowering elimina o marcador preservando o corpo comum. Não
apresentar este documento como evidência de que o protocolo já está implementado
ou validado. Ao implementar, atualizar este parágrafo com o estado real.

### Elementos fundamentais e responsabilidades

| Elemento | Comportamento esperado |
| --- | --- |
| Contexto transacional | Identifica uma transação lógica e sua tentativa ativa; mantém profundidade, objetos registrados, reservas e buffers privados. |
| Lista de objetos | Registra objetos elegíveis no contexto, sem ler seus valores nem adquirir reservas antecipadamente. Listas internas ampliam o conjunto. |
| Hash global | Localiza registros por endereço canônico e mantém sua vida útil. Não decide conflito, commit ou abort. |
| Registro global da política | Mantém leitores identificados e eventual dono da escrita de cada objeto. Todas as alterações usam o mutex de seu bucket. |
| Registro privado por objeto/contexto | Mantém a participação, o modo de acesso, a indicação de escrita e os dados privados necessários à tentativa. |
| Reserva compartilhada | Permite leitura por várias transações; impede que outra obtenha escrita enquanto algum leitor a mantiver. |
| Reserva exclusiva | Permite escrita pela transação dona e seus próprios acessos posteriores; impede acessos de outras transações. |
| Mutex do bucket | Protege operações curtas na estrutura e nos metadados. Não é a reserva transacional e não fica adquirido durante o corpo. |
| Buffer privado | Recebe escritas ainda invisíveis às demais transações e atende leituras dos valores já modificados pela própria transação. |
| Controle de repetição | Encerra a tentativa conflitante, limpa seu estado, aguarda e recomeça a região externa inteira. |

O conjunto registrado, o conjunto de leitura e o conjunto de escrita não são
necessariamente iguais. Um objeto listado e nunca acessado não provoca conflito.
O conjunto de leitura contém objetos com reserva de leitura; o de escrita contém
objetos com reserva exclusiva e modificações pendentes. A política pode manter
histórico adicional de acessos, mas não precisa de contadores de versão para
validar leituras enquanto as reservas persistirem.

### Ciclo de vida de uma tentativa

1. **Entrada externa:** criar/ativar o contexto e iniciar uma tentativa com
   profundidade um. Registrar a lista externa sem acessar o valor dos objetos.
2. **Execução ativa:** cada acesso a objeto transacionado verifica e adquire ou
   confirma a reserva necessária antes de ler/escrever. Temporários privados da
   tentativa seguem as regras de restauração, sem registro obrigatório na hash.
3. **Entrada/saída interna:** incrementar/decrementar profundidade, compartilhar
   a identidade e acumular registros. A saída interna não publica nem libera
   reservas. A tentativa continua ativa no contexto externo.
4. **Confirmação externa:** se não houve abort, preparar a publicação, conservar
   todas as reservas e publicar todos os valores pendentes. Em seguida retirar
   reservas e participações, liberar referências e buffers e desativar o contexto.
5. **Conflito:** impedir o acesso solicitante e marcar a tentativa para abort.
   Não continuar executando normalmente, não publicar e não abortar a transação
   que já detém a reserva incompatível.
6. **Abort e repetição:** executar os cleanups suportados com o estado ainda
   válido, descartar modificações privadas, retirar reservas/participações e
   liberar referências. Desfazer todos os níveis internos de uma só vez, aguardar
   sem reservas e iniciar uma tentativa nova na entrada externa.

Estados conceituais: `ATIVA -> EM_CONFIRMACAO -> CONCLUIDA` no caminho de sucesso;
`ATIVA -> EM_ABORT -> EM_ESPERA -> ATIVA` no caminho de repetição. São descrições
do protocolo, não nomes obrigatórios de enums. Uma tentativa em confirmação não
pode voltar a abortar depois de publicar a primeira escrita. A tentativa seguinte
não herda reservas, registros dinâmicos ou buffers da tentativa abortada: as
listas são processadas novamente conforme a execução. Contadores de backoff
podem persistir entre tentativas; dados do programa não podem vazar pelo reinício.

### Matriz de decisões de acesso

A comparação considera sempre outra transação lógica. Reservas da própria
transação, inclusive de regiões internas, não causam conflito.

| Acesso solicitado | Estado mantido por outras transações | Resultado |
| --- | --- | --- |
| Leitura | Nenhuma reserva ou apenas leitores | Conceder/manter reserva compartilhada e realizar leitura. |
| Leitura | Escritor | Abortar a solicitante antes da leitura. |
| Escrita | Nenhuma reserva | Conceder reserva exclusiva e escrever na cópia privada. |
| Escrita | Um ou mais leitores ou escritor | Abortar a solicitante antes da escrita. |
| Conversão da própria leitura para escrita | Nenhuma reserva de outra transação | Converter para exclusiva e escrever na cópia privada. |
| Conversão da própria leitura para escrita | Leitor ou escritor de outra transação | Abortar a solicitante. |
| Leitura/escrita com reserva exclusiva própria | Nenhuma reserva incompatível, por invariável | Usar o estado privado da própria transação. |

Não confundir conflito de hash com conflito transacional. Dois endereços que
caem no mesmo bucket apenas disputam brevemente seu mutex; podem pertencer a
objetos independentes. Um conflito transacional envolve o mesmo objeto canônico
e modos de acesso incompatíveis, mesmo sem colisões de hash.

### Garantias e limites da política

Para programas no subconjunto suportado e com todos os acessos relevantes
instrumentados, reservas mantidas até o término estabilizam as leituras entre
transações e impedem observação de publicação parcial. A confirmação lógica
acontece após publicar todos os objetos e antes de liberar a primeira reserva.
Não é uma fotografia global tomada na entrada: objetos diferentes podem ter
suas reservas adquiridas em instantes diferentes.

A política não garante isolamento em relação a acessos externos ao protocolo,
justiça entre transações nem limite de repetições. Abort por conflito não é erro
final do programa e não é exceção do usuário. Falha de recurso, uso não suportado
e exceção não devem ser convertidos automaticamente em repetição infinita.

A implementação inicial usa espera progressiva limitada por tentativa, com
número ilimitado de tentativas. A espera reduz colisões repetidas, mas não prova
progresso. Não há versão publicada antes do commit nem rollback da memória
original para objetos transacionados, pois as escritas permanecem privadas;
o abort ainda precisa limpar reservas, temporários e demais recursos da tentativa.

As restrições e questões abertas nas seções seguintes fazem parte do contrato.
Em particular, este resumo não resolve implicitamente leituras externas não
listadas, exceções, aliases não analisáveis ou efeitos externos não suportados.

### Granularidade e conflitos

Cada objeto listado é uma unidade de conflito inteira. Acessar um campo ou
elemento conta como acesso ao objeto que o contém, com identificação por sua
chave canônica. A instrumentação não pode consultar apenas o endereço do campo
ou elemento e concluir que ele não está registrado. O mapeamento de subacessos
para o objeto listado é responsabilidade do compilador/runtime de acesso.
Sobreposições entre objetos registrados distintos não são suportadas no
protótipo; diagnosticar os casos identificáveis e documentar a restrição para
os demais. Hash por igualdade exata não detecta sobreposição de intervalos.

Verificar conflitos antes de cada acesso transacional. Entre transações
diferentes, leitura/leitura é compatível; leitura/escrita, escrita/leitura e
escrita/escrita conflitam. Uma transação não conflita consigo mesma, inclusive
no flat nesting. Abortar aquela que tenta realizar o acesso conflitante,
antes de realizar o acesso. Registro na lista não é uma leitura nem uma escrita:
a reserva é adquirida no primeiro acesso correspondente.

### Reservas, versões e validação

Manter leitores identificados e dono da reserva de escrita nos metadados da
política de cada entrada. Uma leitura obtém reserva compartilhada quando não
há escritor de outra transação. Uma escrita exige reserva exclusiva: não pode
haver escritor nem leitores de outras transações. Converter a própria reserva
de leitura para escrita somente se não houver outro leitor; se houver, abortar
a transação que tenta a conversão. Verificação e atualização da reserva ocorrem
juntas sob o mutex do bucket. Não manter esse mutex durante todo o bloco.

As reservas persistem até confirmação ou abort da transação externa, mesmo
quando o objeto só foi usado na região interna. Referências físicas e reservas
são coisas distintas. Uma referência na hash não dá permissão de acesso.

As escritas são adiadas: manter uma cópia privada dos novos valores por objeto
e contexto, sem publicar modificações antes do commit. Leituras da própria
transação enxergam seus valores pendentes. Escritas parciais devem preservar os
bytes não modificados do objeto, respeitando suas restrições de tipo; não
prescrever cópia bruta para toda classe C++ sem definir os tipos suportados.
Inicializar a cópia somente depois de obter a reserva necessária; `register`
não pode ler antecipadamente o valor original sem reserva. Uma escrita parcial
precisa preservar a parte não modificada sob reserva exclusiva. Uma leitura
não deve introduzir leitura tipada de campos não inicializados nem de padding.
A cópia é armazenamento interno: `&x` continua representando o endereço lógico
do objeto original, não pode expor o endereço do buffer privado. Desreferências
suportadas são redirecionadas pela instrumentação, preservando comparações de
ponteiros e o mapeamento objeto/deslocamento.

Com todas as reservas mantidas, os objetos lidos não podem ser modificados por
outra transação até o fim. Portanto esta política não exige contadores de versão
nem revalidação global do conjunto de leituras no commit. Isso depende de todos
os acessos transacionais relevantes estarem instrumentados e de as reservas
serem respeitadas. A implementação deve validar suas invariantes de propriedade
antes de publicar; referências vivas ou presença na hash não são validação.

### Confirmação e abort

Na saída externa bem-sucedida, manter todas as reservas, publicar todas as
escritas privadas e somente depois liberar qualquer reserva. Esse protocolo
impede que outra transação respeitando o protocolo observe publicação parcial,
mesmo quando os objetos pertencem a buckets diferentes. A publicação deve
obedecer à ordenação de memória estabelecida pelos locks de liberação e
posterior aquisição das reservas. Não liberar um objeto logo após copiá-lo
quando ainda houver outros objetos a publicar.
Preparar alocações, verificações e recursos que possam falhar antes de iniciar
a publicação. Depois da primeira escrita publicada, não permitir abort por
conflito nem executar código do usuário ou operação potencialmente lançadora;
a fase de publicação deve terminar antes da liberação de reservas.

Conflito antes de um acesso causa abort da transação lógica inteira: descartar
escritas privadas, retirar todas as reservas/participações e liberar referências
do contexto. Reiniciar a partir da entrada externa, inclusive quando o conflito
ocorreu em uma região interna ou função chamada. Não executar o commit de uma
transação abortada. A implementação de reinício deve respeitar cleanups e vida
útil de objetos C/C++; não usar salto não local arbitrário atravessando destrutores.
Primeiro impedir a continuação normal e executar os cleanups suportados da
tentativa enquanto contexto e dados privados ainda são válidos; depois descartar
buffers e liberar reservas/participações/referências, e só então esperar.
Não permitir acesso a buffers já descartados durante a limpeza. Se os cleanups
não puderem cumprir esse protocolo, o caso fica fora do subconjunto suportado.

### Escalonamento e progresso

Após limpar a tentativa abortada, aguardar entre tentativas usando backoff com
espera progressiva e teto por espera; reiniciar sem limite de tentativas. Não
manter reservas nem mutexes durante a espera. Os parâmetros temporais e eventual
jitter são detalhes ajustáveis da implementação, a documentar e testar; usar
aritmética limitada, sem overflow em contadores de tentativas ou de espera.

Não há prioridade por idade nem fila transacional nesta política: quem tenta
um acesso conflitante aborta. Não esperar pelo dono do objeto mantendo reservas.
Isso evita ciclos de espera entre reservas transacionais, mas não prova ausência
de deadlock em locks do usuário ou no restante do runtime. Mutexes por bucket
tornam a tabela bloqueante. Backoff não garante ausência de starvation/livelock,
justiça ou conclusão em tempo limitado; nenhuma dessas garantias foi assumida.

### Código não transacional e efeitos externos

Acessos fora de transaction ficam fora do protocolo, sem isolamento forte contra
código não transacional e sem alteração das regras C/C++ sobre corridas de dados.
Uma escrita externa não será detectada por validação transacional inexistente.

Restringir operações cujo efeito não pode ser descartado ou repetido com
segurança: entrada/saída, arquivos, chamadas de sistema, chamadas externas sem
suporte e criação de tasks. Diagnosticar casos identificáveis e documentar a
pré-condição para chamadas indiretas ou outros casos não verificáveis. Não
anunciar diagnóstico completo por análise apenas local. Alocação/liberação,
atomics, volatile, locks do usuário e exceções exigem classificação explícita
antes de serem admitidos; a política inicial não lhes concede suporte implícito.

### Tipos suportados no protótipo

Aceitar objetos escalares comuns, incluindo ponteiros, e arrays/estruturas de
tamanho conhecido compostos recursivamente desses tipos. Exigir tipo completo
e tamanho conhecido para os objetos registrados. Inicialmente não suportar VLA,
`volatile`, objetos atômicos nem classes C++ com operações especiais de cópia ou
destruição que impeçam o tratamento como dados triviais. Não permitir escritas em objetos
`const`, mesmo que a cópia privada seja mutável. Não remover qualificadores para
contornar restrições da linguagem. Verificar recursivamente
os componentes de agregados. Não inferir que toda classe C++ pode ser manipulada
por cópia bruta; admitir agregados triviais apenas após verificar as propriedades
necessárias segundo a linguagem. Tipos não contemplados exigem extensão explícita,
não aceitação silenciosa.

Listar um ponteiro registra o objeto ponteiro e seu valor, não a memória apontada.
Para transacionar o objeto apontado, esse objeto precisa estar no conjunto listado
e ser identificado pela instrumentação. Referências C++ designam o objeto
referido, desde que ele atenda às mesmas restrições. Preservar tipos e
qualificadores durante a análise; emitir diagnóstico adequado ao caso rejeitado.

### Aliases e chamadas de funções

Suportar acessos diretos e aliases que o compilador consiga resolver para um
objeto registrado. Um alias deve usar a mesma entrada e a mesma cópia privada,
sem acessar a memória original por um caminho alternativo. Campos e elementos
usam a identidade do objeto inteiro e deslocamentos dentro de sua cópia privada.
O suporte não exige uma análise perfeita de ponteiros: restringir aliases não
resolvidos, diagnosticar casos identificáveis e documentar os demais limites.
Não afirmar cobertura de aliases arbitrários nem busca de intervalos oferecida
pela hash por igualdade exata.

Admitir chamadas comprovadamente livres de efeitos incompatíveis com repetição
e chamadas a versões explicitamente preparadas para execução transacional. Uma
função que acessa objetos registrados precisa propagar o contexto ativo e usar
o protocolo de reservas e cópias privadas, inclusive em acessos por argumentos.
Uma classificação `pure`/`const` isolada não prova que leituras de objetos
transacionados vejam as cópias privadas. Não tratar inlining como prova de
segurança: os acessos resultantes ainda precisam ser instrumentados.

Chamadas externas ou indiretas sem suporte ficam restritas. Definir mecanismo
explícito de identificação/geração das versões transacionais antes de admiti-las;
esta especificação não inventa atributo ou ABI já existente. Funções preparadas
também obedecem às restrições sobre efeitos externos, tipos, aliases e reinício.

### Estado restaurado entre tentativas

Toda escrita em objeto cuja vida começou antes da transação externa deve atingir
um objeto listado no contexto compartilhado, seja pela lista externa ou por uma
lista interna já processada. Rejeitar escritas identificáveis em objetos externos
não listados; não ampliar automaticamente o conjunto transacionado. Uma lista
interna não autoriza retroativamente um acesso anterior ao seu processamento.
Aplicar a regra também a aliases, funções chamadas e efeitos indiretos.

Variáveis automáticas criadas durante uma tentativa podem servir de estado
temporário. Devem ser recriadas no reinício, com inicializadores novamente
executados, e não podem escapar da tentativa. Não confundir uma variável local
`static` com um objeto novo a cada tentativa. Temporários não precisam ser
registrados quando são privados à tentativa e não compartilhados; se forem
explicitamente listados, obedecem às restrições de vida útil já estabelecidas.

O abort executa os cleanups suportados enquanto o contexto ainda está válido,
descarta as cópias privadas e limpa reservas/participações, nessa ordem, antes
da espera e da próxima tentativa. Não preservar incrementos, ponteiros escapados ou outro estado visível
produzido por uma tentativa abortada. O mecanismo de controle deve retornar à
entrada externa e reconstruir os escopos temporários, não apenas repetir a
instrução que encontrou conflito. Suporte a destrutores ou cleanups com efeitos
externos não decorre dessa exigência e permanece restrito.

### Pontos de semântica ainda abertos: não preencher implicitamente

As regras acordadas restringem escritas externas à lista, mas não determinam o
tratamento de leituras de objetos externos não listados. É necessário decidir
se serão proibidas, admitidas como leituras comuns fora das garantias
transacionais ou limitadas a dados imutáveis. Essa escolha afeta consistência e
repetibilidade. Até ser decidida, não prometer consistência transacional para
essas leituras nem instrumentá-las como registro automático.

A forma sem lista e o uso de objetos antes de uma lista interna também precisam
ser coerentes com essa decisão. A forma sem lista não autoriza automaticamente
transacionar todos os objetos do corpo.

A expressão “classes triviais” ainda precisa de critérios verificáveis no
front-end. Antes da integração, fixar a matriz de suporte para unions, campos de
bits, membros flexíveis, extensões de vetores e agregados com ponteiros para seus
próprios subobjetos. Casos não contemplados são restritos; não ampliar o conjunto
somente porque `sizeof` é conhecido. Buffers precisam de alinhamento suficiente
e de uma estratégia de acesso que respeite aliasing e vida útil C/C++.

Exceções do usuário não foram equiparadas a conflito. Definir separadamente se
serão rejeitadas ou admitidas com descarte e propagação segundo as regras de
bloco estruturado. Nunca reiniciar indefinidamente uma exceção como se fosse
conflito nem confirmar durante seu desenrolamento.

Tasks, suspensão e sincronizações coletivas dentro da transação exigem restrição
explícita: uma barreira dentro de código que pode ser repetido por apenas uma
thread pode impedir progresso. Rejeitar construções identificáveis não suportadas,
incluindo barreiras/worksharing/parallel internos, até definir seu protocolo.
Executar transaction dentro de uma região parallel externa continua sendo o
cenário pretendido; isso não autoriza herança automática do contexto por threads.

## Arquivos e organização

Implementar em C, seguindo as convenções locais da libgomp:

- `libgomp/newtm-hash.c`: implementação da tabela, cálculo de hash,
  encadeamento, alocação, sincronização e contagem de referências.
- `libgomp/newtm-hash.h`: interface interna, tipos opacos, contratos e código
  de estado. Não instalar esse cabeçalho como API pública OpenMP.
- `libgomp/newtm-transaction.c` e `libgomp/newtm-transaction.h`: camada futura
  de contexto e política transacional. Esses arquivos são pontos previstos de
  integração, não requisito para entregar apenas a biblioteca hash.
- `OPENMP_TRANSACTION_HASH_AGENT.md`: manter esta especificação coerente com
  as decisões e limitações da implementação efetivamente entregue.

Usar prefixo `newtm_hash_` nas primitivas e tipos da tabela. Pesquisar colisões
antes de introduzir nomes. Acrescentar o fonte a `libgomp/Makefile.am` e tratar
os arquivos gerados segundo a infraestrutura e versão de ferramentas do projeto,
sem regenerações amplas desnecessárias. Examinar as regras locais para incluir
os cabeçalhos na distribuição. Reutilizar mutexes e mecanismos de alocação e
falha da libgomp, sem introduzir dependência direta de pthread em código comum.

## Modelo de dados

Tipos opacos expostos pelo cabeçalho:

```c
struct newtm_hash_table;
struct newtm_hash_entry;
```

Cada tabela mantém um vetor de buckets e sua capacidade imutável. Cada bucket
mantém sua lista, seu número de entradas e seu próprio mutex. Cada entrada mantém:

- endereço do objeto, usado como chave e nunca desreferenciado pela tabela;
- tamanho em bytes informado pelo chamador;
- contagem de referências que mantêm a entrada viva;
- ponteiro opaco para metadados da política, inicialmente nulo;
- ligação interna da lista de colisões.

O encadeamento e os mutexes são privados. Não armazenar o valor do objeto nem nós
`tree`/GIMPLE nessa estrutura. Um indicador de escrita pertence ao registro
objeto/transação da política, não a um booleano global compartilhado sem
identidade de transação. Versões, leitores e reservas também pertencem à
política. Na política inicial, leitores e dono da escrita são obrigatórios;
versões numéricas não são necessárias.

Cada aquisição representa uma referência, não necessariamente uma transação
única. A tabela deve equilibrar cada aquisição com uma liberação. A camada
transacional elimina registros duplicados por endereço dentro do contexto
compartilhado: mantém uma referência de participação por objeto nesse contexto,
mesmo que ele seja listado várias vezes ou em diferentes níveis de aninhamento.
Uma aquisição temporária adicional, se realizada, deve ser liberada imediatamente
após identificar a duplicata. Tamanhos incompatíveis continuam sendo erro;
a deduplicação não pode ocultar essa verificação.

## Interface e primitivas

Assinaturas propostas; usar `size_t` e os qualificadores apropriados no
cabeçalho. Definir estados `NEWTM_HASH_OK`, `NEWTM_HASH_INVALID`,
`NEWTM_HASH_NOT_FOUND`, `NEWTM_HASH_SIZE_MISMATCH`, `NEWTM_HASH_BUSY`,
`NEWTM_HASH_OVERFLOW` em `enum newtm_hash_status`.
Saídas por ponteiro ficam nulas nas falhas, quando aplicável. Falhas não deixam
inserções ou incrementos parciais. Para esta proposta, usar `gomp_malloc` e
`gomp_malloc_cleared`, cujo contrato local encerra o processo via `gomp_fatal`
em falta de memória; portanto não oferecer `NEWTM_HASH_NO_MEMORY`. Verificar
overflows antes de alocar. Uma política recuperável de alocação exigirá revisão
explícita desse contrato, sem depender de um estado que nunca é retornado.
Funções que retornam estado rejeitam tabela nula quando recebem uma tabela,
saída nula quando exigem saída e endereço nulo quando recebem chave. Demais
primitivas exigem argumentos não nulos válidos como pré-condição.

### Criação e encerramento

```c
typedef void (*newtm_hash_policy_destroy_fn) (void *policy_data);
enum newtm_hash_status newtm_hash_create (
  size_t bucket_count, newtm_hash_policy_destroy_fn destroy_policy,
  struct newtm_hash_table **out);
enum newtm_hash_status newtm_hash_destroy (struct newtm_hash_table *table);
```

`newtm_hash_create` aloca uma tabela vazia e inicializa o mutex de cada bucket. O serviço
transacional global chama esta função com `NEWTM_HASH_DEFAULT_BUCKETS`, definido
como 1024. A primitiva rejeita capacidade zero e verifica overflow da alocação.
A função hash deve aceitar qualquer capacidade positiva, não exigir potência
de dois silenciosamente. Não descartar os bits baixos de endereços sem avaliar
objetos com alinhamentos pequenos; misturar os bits antes de selecionar o bucket.

`newtm_hash_destroy` libera buckets, tabela e todos os mutexes somente se não houver
entradas vivas; caso contrário retorna `NEWTM_HASH_BUSY`. Exige ausência de
operações concorrentes: os mutexes dos buckets não podem proteger acessos
que começam depois de ela ser destruída. A instância global dura até o término
do runtime; não destruir a tabela a cada saída de transaction.

### Inserção ou aquisição

```c
enum newtm_hash_status newtm_hash_acquire (
  struct newtm_hash_table *table, const void *address, size_t size,
  struct newtm_hash_entry **out);
```

Localiza a chave; se ausente, cria uma entrada com uma referência e metadados
nulos. Se presente e com o mesmo tamanho, incrementa a contagem e devolve a
mesma entrada. A busca e a alteração da lista e das referências ocorrem sob o mutex do bucket
calculado a partir da chave. Rejeita endereço nulo,
tamanho zero e overflow de contagem. Tamanhos diferentes para a mesma chave
retornam `NEWTM_HASH_SIZE_MISMATCH`; não substituir silenciosamente o tamanho.
Esse caso deverá ser revisto se a política futura admitir registros de subobjetos
com mesmo endereço inicial e extensões diferentes.

### Consulta com aquisição de referência

```c
enum newtm_hash_status newtm_hash_lookup_acquire (
  struct newtm_hash_table *table, const void *address,
  struct newtm_hash_entry **out);
```

Consulta sem criar entrada. Se encontrada, incrementa a contagem sob o mutex do bucket;
se ausente, retorna `NEWTM_HASH_NOT_FOUND`. A referência adquirida impede remoção
concorrente até a liberação. Não oferecer um `find` que devolva um ponteiro
sem proteção de vida útil. Uma referência temporária de consulta também precisa
ser liberada, mas não representa um participante semântico da transação.

### Liberação e remoção

```c
void newtm_hash_release (struct newtm_hash_table *table,
                         struct newtm_hash_entry *entry);
```

Decrementa a contagem sob o mutex do bucket e remove a entrada quando ela chega a
zero. A liberação da memória e dos metadados segue o contrato do destrutor
abaixo. O chamador deve possuir exatamente a referência que está liberando e não
pode usar o ponteiro depois da chamada sem outra referência válida. Entrada de
outra tabela, dupla liberação e uso depois de liberar são erros de programação;
verificações de depuração podem diagnosticá-los, sem prometer validar ponteiros
já liberados. Não expor remoção forçada de uma entrada ainda referenciada.

### Consulta aos dados imutáveis

```c
const void *newtm_hash_entry_address (const struct newtm_hash_entry *entry);
size_t newtm_hash_entry_size (const struct newtm_hash_entry *entry);
```

Devolvem chave e tamanho, imutáveis durante a vida da entrada. Exigem uma
referência válida mantida pelo chamador; não acessam o objeto do programa.

### Acesso aos metadados da política

```c
typedef void (*newtm_hash_visit_fn) (const void *address, size_t size,
                                     void **policy_data, void *argument);
void newtm_hash_entry_visit (struct newtm_hash_table *table,
                             struct newtm_hash_entry *entry,
                             newtm_hash_visit_fn visit, void *argument);
```

Executa uma operação curta sobre os metadados, sob o mutex do bucket da entrada, exigindo
uma referência válida. Permite à política consultar ou atualizar seu próprio
estado sem expor a estrutura interna. O callback não pode reentrar na tabela,
esperar por outra transação, executar código do usuário ou adquirir locks que
produzam inversão de ordem. Não executar o corpo transaction sob esse mutex.
Um ponteiro copiado para fora do callback não permanece protegido pelo mutex.

A propriedade de `policy_data` é da entrada após sua instalação pelo callback.
`newtm_hash_create` recebe um destrutor imutável `destroy_policy`, que pode ser
nulo apenas se os metadados permanecerem nulos. Na última liberação, retirar a
entrada do bucket sob seu mutex, decrementar o número de entradas desse
bucket, soltar o mutex, chamar o destrutor se os metadados
forem não nulos e finalmente liberar a entrada. O destrutor não pode acessar a
entrada retirada, reentrar na tabela nem depender de sua presença nela. Uma
nova entrada com a mesma chave pode existir durante essa limpeza; o destrutor
libera somente recursos da geração retirada, nunca altera o objeto registrado
ou metadados de outra geração.

Se `visit` substituir um ponteiro não nulo, o callback é responsável por liberar
ou transferir corretamente os metadados anteriores, respeitando as restrições de
execução sob o mutex. Preferir manter o mesmo objeto de metadados durante a vida
da entrada. Não permitir que o último participante semântico destrua metadados
ainda consultados por referências temporárias; a última referência física é que
dispara o destrutor.

### Estatística mínima

```c
size_t newtm_hash_count (struct newtm_hash_table *table);
```

Obtém o número total de entradas, não a soma de referências. Para fornecer um
retrato consistente, adquire todos os mutexes em ordem crescente de índice,
soma os contadores dos buckets e libera os mutexes em ordem inversa. Se a soma
exceder `SIZE_MAX`, devolve `SIZE_MAX` como contagem saturada; esse comportamento
deve estar documentado no cabeçalho. O incremento do contador de cada bucket
também deve rejeitar overflow antes da inserção. Não adicionar um contador global protegido por mutex no caminho
normal. A contagem é destinada a diagnóstico, pode bloquear todos os buckets
e não deve ser chamada em cada acesso transacional. Seu resultado pode mudar
assim que os locks forem liberados. Não acrescentar iteração pública ou redimensionamento sem
necessidade demonstrada.

## Limites de concorrência e de substituição de política

Contagem de referências garante vida útil, não indica quantas transações estão
ativas. `policy_data` deve manter separadamente as participações e identidades.
Na saída, a camada transacional retira sua participação sob `entry_visit` antes
de liberar sua referência. A última referência só pode ser liberada após retirar todas as participações e
reservas que ela sustenta. O destrutor dos metadados não substitui a liberação
do protocolo transacional. Uma consulta temporária pode manter a entrada na
tabela depois de terminar a última participação: presença na hash não significa
que há conflito ou transação ativa. Não expor a contagem física como contador
de participantes.

Busca, inserção, aquisição/liberação de referência e acesso aos metadados usam
somente o mutex do bucket correspondente. A contagem de referências não precisa
ser atômica, pois todas as suas alterações usam esse mesmo mutex. Endereço e
tamanho continuam consultáveis sem lock enquanto o chamador mantém uma referência.
Manter uma referência não mantém o mutex adquirido. Guardar na entrada um índice
de bucket imutável ou recalculá-lo da chave enquanto a referência está válida.

Inicializar todos os buckets antes de publicar a tabela. A destruição exige
quiescência de todos os usuários, incluindo callbacks e destruidores em andamento;
não basta observar zero entradas enquanto ainda existem operações concorrentes.
Na destruição, verificar primeiro todos os buckets: se algum estiver ocupado,
retornar BUSY sem destruir parcialmente a tabela. Fora da operação de contagem,
nenhuma primitiva deve adquirir vários mutexes de buckets. Callbacks não podem
chamar `newtm_hash_count`, pois ele tenta adquirir novamente o mutex já mantido.

As operações são atômicas por entrada, não por conjunto de objetos. Registrar
`a` e depois `b` não reserva ambos atomicamente. Políticas que precisem de
validação ou aquisição conjunta terão de estender a interface ou usar protocolo
próprio, com ordem de locks definida. Não prometer que qualquer política futura
caberá sem alterações nestas primitivas. A troca de política significa selecionar
outra implementação antes de começar a execução transacional, não trocar os
metadados ou destrutores enquanto há transações ativas.

A referência preserva o registro, não a vida útil do objeto do programa. O
chamador deve garantir que o objeto permanece vivo até terminar a participação.
Para o protótipo, não admitir liberação/reutilização do armazenamento enquanto
existirem referências ao registro daquela instância. Suporte a esse caso exigirá
identidade por geração e protocolo de recuperação adicional.

A exclusão dos acessos externos do protocolo não torna seguros acessos C/C++
concorrentes conflitantes sem sincronização. A tabela não modifica as regras de
corrida de dados da linguagem e não deve prometer resultados definidos nesses
programas. Testes de acesso externo devem evitar corridas de dados.

## Integração com o runtime e o compilador

O compilador gera chamadas; ele não chama `newtm_hash_acquire` para registrar
endereços durante a compilação. Somente o programa em execução conhece os
endereços efetivos de objetos locais e das cópias privadas OpenMP.

### Runtime

A futura camada `newtm-transaction.c` possuirá a instância global, inicializada
uma única vez com sincronização adequada antes de sua publicação. Examinar os
mecanismos existentes em `libgomp/env.c`, `libgomp/libgomp.h` e `critical.c`;
respeitar targets onde `GOMP_MUTEX_INIT_0` é falso. A configuração da capacidade
fica na inicialização, sem alterar uma tabela já em uso.

Interface de ligação sugerida para o compilador, separada da hash:

```c
void *GOMP_transaction_begin (void);
void GOMP_transaction_register (void *context, void *address, size_t size);
void GOMP_transaction_end (void *context);
```

`begin` cria um contexto somente na entrada externa; entradas internas
reutilizam o contexto ativo e incrementam sua profundidade. `register` registra
cada objeto uma única vez nesse contexto. `end` decrementa a profundidade;
somente na saída externa encerra as participações e libera as referências. Essas
assinaturas são uma proposta para integrar o registro, não uma ABI já existente
nem uma implementação de transações completas. A política acordada acima exige
serviços adicionais de acesso e abort/reinício; saída excepcional e integração
com tasks precisam do tratamento restrito descrito neste documento. `end` só
pode confirmar a tentativa externa que permaneceu válida.

Somente os serviços `GOMP_*` chamados pelo código gerado devem integrar a ABI,
com declarações em `libgomp/libgomp_g.h` e exportação versionada coerente em
`libgomp/libgomp.map`. As primitivas `newtm_hash_*` permanecem internas. Não
acrescentar símbolos a uma versão publicada arbitrariamente. A integração deve
preservar a tabela global única entre unidades de tradução que usam o mesmo
runtime; não gerar uma tabela estática em cada arquivo compilado.

### Política de aninhamento: flat nesting

Uma transação lógica possui identidade única, conjunto acumulado de objetos,
profundidade e estado da política. A primeira entrada estabelece profundidade
um; cada entrada interna retorna o mesmo contexto e incrementa a profundidade,
com verificação de overflow. Cada entrada bem-sucedida exige exatamente uma
saída correspondente, inclusive quando a região interna está em uma função
chamada pela região externa. Não inferir aninhamento somente pela estrutura
léxica do GIMPLE: o runtime precisa reconhecer o contexto ativo da execução.

Uma lista interna acrescenta objetos ao conjunto da transação externa. Sair da
região interna não confirma alterações, não encerra participações e não libera
os objetos acrescentados por ela. Esses objetos permanecem associados ao
contexto até a saída externa. Registros repetidos são idempotentes nesse contexto;
threads ou execuções independentes continuam tendo contextos distintos, ainda
que registrem o mesmo endereço. Não detectar conflito da transação consigo mesma.

Nesta política com abort e repetição, um conflito interno afeta a
transação lógica inteira; não há rollback nem repetição independente do bloco
interno. Essa semântica exige implementação explícita dos mecanismos de abort. A saída
externa é o único ponto elegível à confirmação; abort limpa antecipadamente
a tentativa inteira, sem executar saídas normais de níveis já desfeitos. Os caminhos
de exceção devem equilibrar níveis e garantir a limpeza externa sem interpretar
uma exceção automaticamente como confirmação.

A identidade ativa deve acompanhar a execução correta. Armazenamento local à
thread pode atender ao protótipo restrito a execução síncrona sem suspensão,
mas não resolve automaticamente tasks que suspendem ou migram. Não compartilhar
o contexto com outra thread por ela ter sido criada dentro da região. Antes de
habilitar tasking nesse contrato, definir a associação ao contexto de task e as
regras de herança; até lá, declarar essa integração não suportada e diagnosticar
os casos identificáveis, em vez de prometer suporte completo a tasks.

A retenção até a saída externa exige que todos os objetos registrados, inclusive
os introduzidos em regiões internas, permaneçam vivos até esse ponto. Para o
protótipo, objetos cuja vida termine antes do contexto externo não são suportados:
exemplo, uma variável de um sub-bloco listada por uma região interna quando o
sub-bloco termina antes da saída da região externa. Diagnosticar
casos verificáveis e documentar a pré-condição para os demais; não consultar
armazenamento expirado durante confirmação ou rollback. Uma política futura de
retirada antecipada exigirá especificação adicional, sem contrariar flat nesting.

### Front-ends e representação

1. Em `gcc/c/c-parser.cc:newtm_c_parser_transaction` e
   `gcc/cp/parser.cc:newtm_cp_parser_transaction`, resolver e preservar as
   declarações dos nomes no ponto da diretiva, antes de analisar seu corpo.
   Atualmente só há consulta para depuração e descarte. Definir as categorias
   de entidades admitidas em coerência com a política de tipos; variáveis e
   parâmetros são candidatos, funções e typedefs não são objetos transacionáveis.
   Casos especiais como membros e structured bindings exigem tratamento explícito.
2. Em `gcc/c-family/c-omp.cc:newtm_finish_transaction`, sua declaração em
   `c-common.h`, `gcc/tree.def` e `gcc/tree.h`, acrescentar uma representação
   persistente da lista e suas localizações. Não guardar strings para resolver
   nomes posteriormente e não usar ponteiros para tokens temporários.
3. Em `gcc/cp/pt.cc`, separar o caso `OMP_TRANSACTION` quando necessário para
   substituir a lista em templates e validar os casos dependentes. Auditar
   módulos, walkers, cópia e dumps segundo o guia geral.
4. Em `gcc/gimplify.cc`, no caso `OMP_TRANSACTION`, transportar os objetos
   para GIMPLE. Atualizar `gcc/gimple.def`, `gimple.h`, `gimple.cc` e
   `newtm_build_transaction`: o layout atual `GSS_OMP` armazena apenas o corpo.
   Escolher layout que preserve os novos dados e atualizar consumidores.

### Lowering e chamadas geradas

O ponto principal é `gcc/omp-low.cc:newtm_lower_transaction`. Hoje ele apenas
faz `lower_omp` do corpo usando `ctx->outer` e substitui o marcador pelo corpo.
A integração futura deverá gerar, conceitualmente:

```text
context = GOMP_transaction_begin()
para cada objeto listado:
    GOMP_transaction_register(context, endereço efetivo, tamanho em bytes)
corpo transformado
GOMP_transaction_end(context)
```

Essas três chamadas estabelecem somente registro e vida útil. Não constituem
uma ABI completa e independente da política para observar leituras/escritas.
Antes de prometer detecção de conflitos, definir serviços de acesso, identidade
da transação e ordenação com o acesso real à memória. Um simples aviso após
uma escrita pode ser insuficiente para a política escolhida.

O registro deve preceder o primeiro acesso transacional do corpo. Calcular o
endereço da instância correta: uma variável privada/reduction externa não pode
ser substituída pelo endereço de sua declaração original compartilhada.
Referências C++ devem identificar o objeto referido, não um armazenamento
interno de referência. Não interpretar automaticamente um ponteiro listado
como pedido para registrar o objeto apontado. Tipos incompletos, VLA e tamanhos
dependentes exigem tratamento explícito; não assumir `TYPE_SIZE_UNIT` constante.

Em `scan_omp_1_stmt`, tratar os novos operandos e remapeamentos preservando a
transparência atual do contexto externo. Examinar `lower_omp_critical` como
exemplo de geração de chamadas, sem copiar sua semântica de lock ou de exceções.
Os builtins dos serviços devem ser declarados em `gcc/omp-builtins.def`, com
assinaturas em `gcc/builtin-types.def` quando necessário. Não atribuir atributos
`pure`, `const` ou outros que permitam eliminar/reordenar sincronização.

A implementação da política acordada deve instrumentar leituras e escritas enquanto o
marcador e sua lista ainda existem em `newtm_lower_transaction`, antes de
`gsi_replace_with_seq`. Registro de endereços por si só não detecta conflitos.
Aplicar os contratos de aliases e funções definidos acima; não afirmar cobertura
completa apenas pela inspeção de atribuições diretas. Auditar operações que
combinam leitura/escrita, builtins de cópia, acessos agregados e otimizações que
possam remover, mover ou desviar acessos do protocolo.

Garantir liberação nas saídas legais previstas pelo contrato da diretiva,
incluindo a interação com exceções e cleanups C++, sem inventar regras de
propagação. A atual proibição de saltos de blocos estruturados continua válida.
Não é necessário manter o marcador até `omp-expand.cc` se ele continuar sendo
eliminado integralmente em omplower. Auditar LTO e offload quando o escopo
atingir essas camadas; a primeira biblioteca de host não comprova suporte de
dispositivo ou compartilhamento entre host e dispositivo.

## Validação e entrega

Para a biblioteca, verificar colisões, chaves distintas, aquisição repetida,
consulta ausente, tamanho incompatível, liberação parcial/final, contagens,
capacidade inválida e overflow. Verificar o encerramento por falta de memória
conforme contrato, em processo separado se for usado um teste de falha injetada. Testar
concorrência entre threads adquirindo e liberando a mesma chave, incluindo uma
consulta que mantém a entrada viva após a saída de outro participante. Verificar
que operações em buckets distintos podem progredir em paralelo e que colisões
no mesmo bucket continuam corretas. Cobrir contagem concorrente e retorno BUSY
sem destruição parcial. Não
usar temporizações como prova de sincronização. Verificar limpeza de metadados
quando esse suporte existir.

Usar os diretórios e convenções existentes em `libgomp/testsuite`, evitando
exportar API interna apenas para viabilizar testes. Definir um mecanismo de
teste interno apropriado ao build ou testar por serviços públicos quando estes
existirem. Não prometer executar um teste interno que não consegue ser ligado.

Na integração, acrescentar testes C/C++ em
`gcc/testsuite/c-c++-common/gomp`, `gcc/testsuite/g++.dg/gomp` e testes de
execução na libgomp. Cobrir registro antes do corpo e liberação depois dele,
repetições, flat nesting, locais/globais/parâmetros, templates,
reutilização da mesma identidade nos níveis internos, retenção dos objetos
acrescentados internamente até a saída externa e limpeza exatamente uma vez.
Cobrir aninhamento por chamada de função, tamanhos incompatíveis em duplicatas,
limites de profundidade e restrições de vida útil e tasking. Verificar os tipos
aceitos e rejeitados, ponteiro versus objeto apontado, aliases resolvidos usando
a cópia privada, chamadas transacionais e chamadas sem suporte, rejeição de
escritas externas não listadas e recriação de temporários em cada tentativa. Testar leitura/leitura concorrente,
leitura/escrita e escrita/escrita conflitantes, conversão de reserva, descarte no
abort e ausência de publicação parcial. Verificar reservas adquiridas antes da
leitura inicial da cópia, identidade dos ponteiros e ordem dos cleanups. Testar
backoff com parâmetros controlados sem exigir tempo específico ou garantir
conclusão probabilística como asserção de correção.
Cobrir também
objetos privados externos e acesso fora da região sem instrumentação. Conferir
os dumps gimple/omplower e o runtime efetivamente utilizado.

Construir em `/usr/src/gcc-build`, incluindo o runtime modificado; `all-gcc`
sozinho não basta. Ler `.sum` e `.log` e relatar comandos e resultados reais.
As mensagens de depuração existentes nos parsers e em `omp-low.cc` já causam
falhas de saída adicional; distinguir essas falhas de regressões, sem modificar
expectativas para escondê-las. Não afirmar que os resultados históricos dos
outros documentos foram reproduzidos.

A entrega inicial da biblioteca estará completa quando suas primitivas,
propriedade de memória e sincronização estiverem implementadas e verificadas.
Isso não equivale a implementar a semântica transacional. Relatar claramente
quais pontos da integração acima continuam futuros e atualizar o manual se o
comportamento visível da diretiva mudar.

## Decisões ainda necessárias para integrar a diretiva

As políticas de tipos, aliases, funções chamadas e restauração estão definidas
nas seções anteriores. Antes de integrar o compilador, fechar os mecanismos de
reconhecimento de funções transacionais, diagnóstico de aliases não suportados,
reinício C/C++ e cleanups, além do tratamento de exceções, leituras externas
não listadas, matriz precisa dos tipos e comportamento da forma sem lista. A forma sem lista existe hoje, mas não autoriza inferir
implicitamente todos os objetos acessados nem escrever objetos externos não
listados. Aplicar as limitações sobre sobreposição, tasks e efeitos externos e
diagnosticar os casos identificáveis. Esses mecanismos devem ser definidos antes
de implementar a ABI definitiva.

Escolher libgomp como local da biblioteca e remoção automática são propostas
técnicas deste documento. Não apresentá-las como escolhas já aprovadas
individualmente pelo usuário.

## Estado do esqueleto da biblioteca hash

Foram introduzidos `libgomp/newtm-hash.c` e `libgomp/newtm-hash.h`, com os tipos
opacos públicos internos, as estruturas privadas de tabela/bucket/entrada e as
nove primitivas especificadas. Os comentários descrevem argumentos, retornos,
pré-condições e comportamento futuro. As funções ainda não implementam operações:
todos os corpos chamam `gomp_fatal` com diagnóstico de funcionalidade não
implementada, evitando retornos fictícios de sucesso e uso de estado inválido.
Nenhuma chamada a essas primitivas foi adicionada ao compilador ou runtime.

`libgomp/Makefile.am` e seu `Makefile.in` incluem o fonte e o cabeçalho entre
os fontes da biblioteca, com objeto e dependência correspondentes. O cabeçalho
continua interno e os símbolos têm visibilidade oculta. Não foram criados nesta
etapa estruturas ou serviços da camada transacional de política.

Validação desta etapa: compilação de `newtm-hash.lo` e `make -j3 all` em
`/usr/src/gcc-build/x86_64-pc-linux-gnu/libgomp`, concluídos com sucesso,
incluindo compilação com `-Wall -Werror`. Logs:
`/tmp/newtm-hash-skeleton-build.log` e `/tmp/newtm-hash-skeleton-make.log`.
Isso comprova compilação e ligação do esqueleto, não funcionalidade da hash.
