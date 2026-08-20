# Roteiro de apresentação — Simulador de Escalonamento

## Orientações gerais

- **Duração total:** 10 minutos exatos, de `00:00` a `10:00`.
- **Formato sugerido:** sete falas contínuas, sem intervalo entre integrantes.
- **Ritmo:** conversação clara, sem acelerar; cada integrante deve ensaiar respeitando o encerramento indicado para sua fala.
- **Apoio visual:** usar a capa e os onze quadros de `apresentacao/main.tex`. Deixar a capa projetada antes do início e iniciar o cronômetro ao avançar para o primeiro quadro de conteúdo. Alguns blocos usam dois ou quatro quadros, mas a troca deve ocorrer durante a fala, sem pausa. Cada gráfico ocupa um quadro completo para preservar título, cenários, cinco algoritmos, unidade, média e IC95%.
- **Regra de transição:** quem termina chama imediatamente o próximo integrante pela ideia indicada no fim da fala. As transições já fazem parte do tempo de cada bloco.

## Visão do tempo

| Intervalo | Duração | Responsável | Tema |
| --- | ---: | --- | --- |
| `00:00–01:25` | 1min25s | Manoel Junio | Problema, objetivo e arquitetura geral |
| `01:25–02:50` | 1min25s | Elder Rayan | Processos, filas, eventos e SJF |
| `02:50–04:15` | 1min25s | Espedito Ramom | Cargas, configuração e reprodutibilidade |
| `04:15–05:40` | 1min25s | Pedro Yan | Algoritmos clássicos |
| `05:40–07:05` | 1min25s | Sebastião | Algoritmo próprio PDBH |
| `07:05–08:35` | 1min30s | Samuel | Métricas, testes e protocolo experimental |
| `08:35–10:00` | 1min25s | Sabrina | Resultados, análise crítica e conclusão |
| **Total** | **10min00s** |  |  |

## Roteiro falado

### 1. `00:00–01:25` — Manoel Junio: problema e visão geral

**Capa, antes do cronômetro:** título, disciplina, integrantes, instituição e data.

**Quadro 1:** motivação, objetivos da disciplina e fluxo `carga → simulação → métricas → artigo`.

> Boa noite. Nosso projeto é um simulador de escalonamento de processos, desenvolvido em C para estudar como um sistema operacional decide qual processo utiliza a CPU. Essa decisão parece simples, mas afeta o tempo de conclusão das tarefas, a divisão justa do processador e o custo causado pelas mudanças entre processos.
>
> O programa representa um computador com uma CPU e tempo dividido em ticks. O motor acompanha chegadas, execução, bloqueios por entrada e saída, retornos à fila e finalizações. A troca de contexto custa um tick quando a CPU passa diretamente entre PIDs diferentes. Primeiro despacho, retorno após ociosidade e renovação do único processo apto não contam; durante o custo, a CPU fica indisponível, embora chegadas e conclusões de entrada e saída continuem ocorrendo.
>
> Organizamos o projeto em partes independentes: estruturas de processos e filas, motor de simulação, algoritmos de escalonamento, geração de cargas, métricas, testes e scripts para executar e analisar os experimentos. O Makefile e a integração contínua automatizam compilação, verificações e testes. Essa separação permitiu comparar todas as políticas sobre o mesmo modelo, sem favorecer nenhuma delas.
>
> Para mostrar como esse motor representa cada tarefa ao longo do tempo, passo agora para Elder.

### 2. `01:25–02:50` — Elder Rayan: processos, filas, eventos e SJF

**Quadro 2:** ciclo de estados `novo → pronto → executando → bloqueado/pronto → finalizado` e decisões do modelo de E/S.

> Cada processo possui um identificador, instante de chegada, prioridade e uma sequência alternada de rajadas de CPU e de entrada e saída. Durante a simulação, ele passa pelos estados novo, pronto, executando, bloqueado e finalizado. As filas mantêm apenas os processos que realmente podem disputar a CPU, e verificações internas garantem que um processo não apareça em estados incompatíveis ao mesmo tempo.
>
> Quando uma rajada de CPU termina, o processo inicia a entrada e saída e fica bloqueado por sua duração sorteada. Operações de processos diferentes ocorrem em paralelo, sem dispositivo explícito nem fila própria. Ao terminar, o processo volta à fila de prontos. O modelo é igual para todas as políticas; sua limitação é não representar contenção de dispositivos, o que pode favorecer cargas I/O-bound.
>
> Além dos algoritmos principais, implementamos o SJF não preemptivo como comparação adicional. Como um sistema real não conhece a duração futura da próxima rajada, o SJF não recebe essa informação pronta: ele estima a duração a partir do histórico observado do próprio processo. Assim, tarefas com estimativa menor são escolhidas primeiro, sem interromper uma tarefa que já está executando.
>
> Como esses processos são criados de modo controlado e repetível é o assunto que Ramom apresenta agora.

### 3. `02:50–04:15` — Espedito Ramom: cargas, configuração e reprodutibilidade

**Quadro 3:** quatro cenários e a sequência `seed → workload CSV → mesmos algoritmos`.

> Para que a comparação seja justa, todos os algoritmos recebem exatamente os mesmos processos. O gerador PCG32 é controlado por seed: repetindo seed e cenário, obtemos a mesma carga. O primeiro processo chega no tick zero; os demais usam intervalos aleatórios acumulados de zero a três ticks, evitando concentrar tudo no início. O CSV exportado e importado preserva cada chegada, prioridade e rajada.
>
> Criamos quatro cenários. O equilibrado mistura perfis; o I/O-bound favorece operações de entrada e saída; o CPU-bound contém rajadas maiores de processamento; e o desbalanceado concentra processos de alta prioridade. A configuração pode vir dos valores internos, de um arquivo ou da linha de comando, nessa ordem de precedência. Entradas inválidas e combinações incoerentes são rejeitadas antes da execução.
>
> O piloto e o executor em lote automatizam várias simulações. O executor registra o progresso, permite interromper e continuar depois e valida os resultados já existentes antes de reutilizá-los. Cada workload recebe ainda um hash SHA-256, uma espécie de impressão digital, presente também nos resultados. Isso demonstra que políticas diferentes foram realmente testadas com a mesma entrada.
>
> Com a carga fixada, Pedro explica quais decisões os escalonadores clássicos tomam.

### 4. `04:15–05:40` — Pedro Yan: algoritmos clássicos

**Quadro 4:** tabela com FCFS, Round Robin, Prioridade e SJF; colunas “critério” e “interrompe?”.

> Construímos uma interface comum para que o motor da simulação converse com qualquer escalonador da mesma forma. Assim, muda apenas a regra de escolha do próximo processo, enquanto eventos, custos e métricas continuam iguais.
>
> O FCFS segue a ordem de entrada na fila de prontos. Ele é simples e não interrompe uma rajada, mas um processo longo pode fazer vários curtos esperarem. O Round Robin usa fila circular e quantum configurável, fixado em quatro ticks no experimento. Se o quantum termina e existem outros processos aptos, o atual volta à fila. Essa alternância distribui o acesso, mas aumenta as trocas de contexto.
>
> A Prioridade escolhe o menor valor numérico, que representa maior prioridade. Ela é não preemptiva: uma chegada mais prioritária espera a rajada atual acabar. Empates usam tempo na fila e identificador. Como comparação extra, o SJF também é não preemptivo e estima a próxima rajada pelo histórico observado, sem conhecer o futuro.
>
> Esses três algoritmos formam nossa referência. A equipe então propôs uma política que tenta reduzir a espera excessiva causada por prioridades fixas. Sebastião apresenta essa proposta.

### 5. `05:40–07:05` — Sebastião Sousa: algoritmo próprio PDBH

**Quadro 5:** motivação, inspiração e fórmula `prioridade base − bônus de espera + penalidade de CPU`.

> Nosso algoritmo próprio se chama Prioridade Dinâmica Baseada em Histórico, ou PDBH. Ele nasceu para enfrentar a inanição, quando processos de baixa prioridade esperam excessivamente. A proposta combina ideias conhecidas de envelhecimento e realimentação, mas introduz uma pontuação própria que considera espera e CPU já consumida.
>
> Em vez de olhar apenas a prioridade inicial, o PDBH calcula uma pontuação a cada escolha. Quanto mais um processo espera, maior é seu bônus; quanto mais CPU já consumiu, maior é sua penalidade. O cálculo usa somente o estado atual e o histórico observado, sem conhecimento do futuro. Em caso de empate, vence quem está pronto há mais tempo e, depois, o menor identificador.
>
> A expectativa era equilibrar a preferência original com oportunidades para quem esperou muito ou já recebeu pouca CPU. Mantivemos a política não preemptiva para compará-la de modo direto com FCFS e Prioridade: a pontuação decide o próximo despacho, mas não interrompe uma rajada em andamento.
>
> Os experimentos revelaram uma limitação: o bônus de vários processos cresce quase ao mesmo tempo e frequentemente preserva a ordem de chegada. Reconhecer esse resultado negativo foi parte valiosa da avaliação. Antes de vê-lo, Samuel explica como medimos e validamos o comportamento.

### 6. `07:05–08:35` — Samuel Wagner: métricas, testes e experimento

**Quadros 6 e 7:** definições das três métricas e protocolo `4 cenários × 100 seeds × 5 algoritmos = 2.000 execuções`.

> Avaliamos cada política por três perspectivas. Turnaround é conclusão menos chegada. Trocas de contexto contam mudanças diretas entre processos. Slowdown divide o turnaround pela soma ideal de CPU e entrada e saída; o índice de Jain compara esses slowdowns. Próximo de cem por cento significa atrasos mais equilibrados, mas justiça deve ser lida junto ao turnaround: todos podem ser igualmente lentos.
>
> O experimento final combina quatro cenários, cem seeds e cinco algoritmos, totalizando duas mil execuções com mil processos em cada carga. Os scripts consolidam os CSVs, calculam média e intervalo de confiança de 95% e geram os três gráficos apresentados no artigo. Manifestos registram configuração, versão do código e hashes dos artefatos, permitindo auditar e repetir o lote.
>
> A confiabilidade não depende apenas do resultado final. Há testes de processos, filas, escalonadores, simulação, configuração, importação e exportação de workloads, linha de comando, análise e evidências. Também verificamos determinismo, transições válidas, cálculos das métricas, retomada do experimento e publicação segura dos arquivos, evitando resultados incompletos.
>
> Com esse protocolo comum e verificável, Sabrina pode apresentar o que os dados realmente mostraram.

### 7. `08:35–10:00` — Sabrina Alencar: resultados e conclusão

**Quadros 8 a 11:** um quadro completo para cada gráfico — turnaround, Jain e trocas — e a síntese “Prioridade: menor turnaround”, “RR: maior justiça e mais trocas”, “PDBH ≈ FCFS”.

> Não existe um único algoritmo melhor em tudo. A Prioridade Estática apresentou o menor turnaround médio nos quatro cenários, mas teve a pior equidade. Nos cenários equilibrado e desbalanceado, seu índice de Jain ficou próximo de 31%, sinal de grande desigualdade entre processos.
>
> O Round Robin seguiu o comportamento oposto. Ele alcançou boa justiça, chegando a aproximadamente 96% no cenário CPU-bound e 97% no I/O-bound. Porém, pagou por isso com maior turnaround e muitas trocas de contexto: no CPU-bound foram mais de vinte mil, contra cerca de duas mil nas políticas não preemptivas.
>
> O resultado mais importante foi o PDBH. Seus intervalos de confiança ficaram praticamente sobrepostos aos do FCFS. Como ele não interrompe a rajada atual, os processos aguardam juntos e acumulam bônus parecidos; na prática, a pontuação tende a reproduzir a ordem de chegada, acrescentando cálculo sem ganho mensurável.
>
> O experimento respondeu à pergunta proposta: histórico dinâmico, sozinho e sem preempção, não melhorou o escalonamento neste modelo. Entregamos código testado, cargas reproduzíveis, duas mil execuções auditáveis, gráficos e artigo, além de um caminho futuro: investigar uma versão preemptiva do PDBH. Obrigada.

## Base da atribuição das falas

As falas foram associadas ao histórico Git e às responsabilidades registradas no projeto, considerando commits incorporados ao ramo principal e os PRs correspondentes:

- **Manoel Junio:** estrutura inicial, Makefile, CI e motor de simulação por eventos (issues #2 e #5; PR #24 e PR #28 no histórico).
- **Elder Rayan:** estruturas de processo e fila, invariantes da simulação, SJF e finalização do artigo (issues #4, #12, #17 e #21; PRs #25, #34, #40 e #43).
- **Espedito Ramom:** RNG, validação de argumentos, geração/importação de workloads, piloto e executor experimental (issue #10 e contribuições posteriores; PRs #26, #33 e #37).
- **Pedro Yan:** interface comum de escalonadores, FCFS, Round Robin e Prioridade não preemptiva (issues #7 e #8; PRs #32 e #35).
- **Sebastião Sousa:** concepção, implementação e documentação do PDBH (issue #9; PR #37).
- **Samuel Wagner:** especificação, trocas de contexto, métricas, persistência reproduzível, testes, protocolo, análise e evidências experimentais (issues #1, #6, #13, #14, #15 e #30; PRs #29, #31, #36, #38 e #39, além das revisões finais).
- **Sabrina Alencar:** consolidação dos resultados, gráficos, análise dos algoritmos, discussão estatística e revisão do artigo (issues #15, #16 e contribuições de análise no histórico; PRs #39 e #41).

Essa seção é apenas para organização e prestação de contas; não deve ser lida durante os 10 minutos.
