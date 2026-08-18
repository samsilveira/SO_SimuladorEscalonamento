# Simulador de Escalonamento de Processos em C

## Descrição

Este projeto consiste em um simulador de escalonamento de processos desenvolvido para a disciplina de Sistemas Operacionais. O objetivo é aplicar conceitos de estados de processos, filas de prontos, bloqueio por E/S, preempção e troca de contexto. O simulador é capaz de gerar cargas de trabalho controladas por *seed*, executar algoritmos clássicos de escalonamento (como FCFS, Round Robin e Prioridade) e comparar os resultados com um algoritmo proposto pela própria equipe, utilizando métricas quantitativas de desempenho e justiça.

## Integrantes

| Nome | GitHub | Contribuições Resumidas |
| --- | --- | --- |
| Elder Rayan Oliveira Silva | @eldrayan | *A definir* |
| Espedito Ramom Mascena Ricarto | @RamomRicarto | *A definir* |
| Manoel Junio Duarte da Silva | @Junio404 | *A definir* |
| Pedro Yan Alcantara Palácio | @pedropalacioo | *A definir* |
| Sabrina Alencar Soares | @sabrinaalencar | *A definir* |
| Samuel Wagner Tiburi Silveira | @samsilveira | *A definir* |
| Sebastião Sousa Soares | @SebastiaoSoares | *A definir* |

## Tecnologias e Mecanismos

- **Linguagem:** C
- **Build:** GCC e Make
- **Validação dinâmica:** AddressSanitizer e UndefinedBehaviorSanitizer no build de desenvolvimento
- **Reprodutibilidade:** PCG32, workload CSV normalizado e SHA-256 implementado no próprio projeto

## Regras da Simulação

- O simulador utiliza tempo discreto em ticks inteiros de 64 bits.
- Cada processo é uma entidade com atributos como tempo de chegada, prioridade, rajadas de CPU e requisições de E/S.
- Processos em E/S são bloqueados e retornam à fila de prontos após a conclusão.
- Uma troca de contexto só é contada e cobrada na mudança direta entre PIDs diferentes, sem intervalo positivo de ociosidade. Primeiro despacho, saída para ociosidade, despacho após ociosidade e renovação de quantum do único processo apto não contam.
- O custo padrão é 1 tick. Custo zero exige `--allow-zero-context-switch-cost` e identifica a execução como análise complementar.
- Durante a troca, chegadas e conclusões de E/S continuam ocorrendo, mas o processo reservado no início da troca não é substituído.
- O simulador calcula `turnaround = conclusão - chegada`, `tempo ideal = ΣCPU + ΣE/S`, `slowdown = turnaround / tempo ideal` e `Jain(%) = (Σslowdown)² / (n × Σslowdown²) × 100`.
- Tempos e acumuladores usam 64 bits; entrada vazia, denominador zero e operações que excederiam o limite são rejeitados.

## Configuração e reprodutibilidade

O simulador aplica a precedência:

```text
padrões internos < arquivo de configuração < argumentos da CLI
```

Arquivos passados por `--config` usam `chave=valor`, aceitam comentários iniciados por `#` e rejeitam chaves desconhecidas, repetidas ou valores inválidos. O arquivo padrão é [`configs/default.conf`](configs/default.conf). Os intervalos de chegada estão congelados em `arrival_min=0` e `arrival_max=3`.

Uma carga só é persistida quando `--workload-output` é informado. `--workload-input` importa uma carga existente, valida integralmente seu schema e exige que a quantidade de PIDs corresponda a `process_count`. Nenhum `load.csv` é criado implicitamente.

O workload normalizado usa:

```text
pid,arrival,priority,burst_index,burst_type,duration
```

Seu SHA-256 é calculado sobre os bytes canônicos e registrado em todo resultado agregado. O round-trip `exportar → importar → exportar` é byte a byte idêntico.

## Pré-requisitos e Dependências

- `gcc`
- `make`
- Opcional para editores: `bear` ou `compiledb` para gerar `compile_commands.json`
- No Windows deste ambiente, use `mingw32-make` se `make` não estiver no PATH

## Estrutura

```text
src/                 código C
include/             cabeçalhos
configs/             configurações chave=valor
tests/               testes mínimos
scripts/             scripts auxiliares
results/raw/         resultados brutos ignorados pelo Git
results/consolidated resultados consolidados versionados
results/figures      gráficos versionados
docs/                especificações e decisões
artigo/              entregável em formato de artigo
```

## Compilação e Execução

```sh
make                 # build otimizado em bin/simulador
make dev             # build com -g e sanitizers no Linux/CI
make release         # build otimizado com -O2
make test            # autoteste do executável
make simulate        # execução simples documentada
make batch           # exemplo de lote em results/raw/
make batch-reduced   # lote rapido: 1 cenario, 2 seeds, 4 algoritmos, 10 processos
make batch-verify    # valida a completude do experimento principal sem executar
make graphs          # prepara diretório de figuras
make analyze         # análise estática do código C com GCC
make compile_commands
make clean           # remove build, bin e results/raw/
```

Execução com configuração padrão e resultado agregado:

```sh
./bin/simulador \
  --config configs/default.conf \
  --algorithm fcfs \
  --run-id equilibrado-fcfs-seed-1 \
  --workload-output results/raw/equilibrado_seed_1_workload.csv \
  --output results/raw/equilibrado_fcfs_seed_1.csv
```

Reutilização da mesma carga com outro algoritmo:

```sh
./bin/simulador \
  --config configs/default.conf \
  --algorithm rr \
  --workload-input results/raw/equilibrado_seed_1_workload.csv \
  --run-id equilibrado-rr-seed-1 \
  --output results/raw/equilibrado_rr_seed_1.csv
```

`--run-id` aceita letras, números, ponto, hífen e sublinhado. Ele é obrigatório quando `--output` ou `--individual-output` é usado. Sem `--output`, o agregado é enviado para a saída padrão com `run_id=adhoc`.

O resultado agregado possui exatamente:

```text
schema_version,run_id,workload_sha256,algorithm,scenario,seed,process_count,context_switch_cost,rr_quantum,makespan,mean_turnaround,context_switches,jain_slowdown_pct,status
```

Métricas individuais não são criadas por padrão. Com `--individual-output arquivo.csv`, o schema é:

```text
run_id,pid,arrival,completion,turnaround,ideal_time,slowdown,priority,total_cpu,total_io,io_requests
```

Valores `double` usam 17 algarismos significativos. Todas as saídas persistentes da execução são escritas primeiro em temporários nos respectivos diretórios e só são publicadas depois que todas foram escritas, descarregadas e fechadas com sucesso. Uma falha preserva os destinos anteriores e remove os temporários. Caminhos equivalentes por `.`/`..` ou links são rejeitados quando causariam colisão entre entradas e saídas.

`make clean` não remove `docs/`, `artigo/`, `results/consolidated/` nem `results/figures/`.

## Executor experimental

O executor da matriz principal usa apenas Python 3 e sua biblioteca padrão. Ele cria
400 workloads, reutiliza cada carga nas quatro políticas e registra as 1.600 execuções
em um manifesto atualizado atomicamente:

```sh
make release
python3 scripts/run_experiment.py --experiment-id main-2026-08
```

Os artefatos ficam em `results/raw/<experiment-id>/{manifest.json,workloads,runs}`.
Uma segunda chamada com os mesmos argumentos retoma o experimento e executa somente
itens ausentes ou inválidos. Commit, tag, hash do executável, configuração efetiva e
matriz precisam coincidir; em caso de mudança, escolha um novo `--experiment-id`.

Para uma validação local/CI rápida, usando exatamente o mesmo fluxo:

```sh
python3 scripts/run_experiment.py --experiment-id smoke --reduced
python3 scripts/run_experiment.py --experiment-id smoke --reduced --verify-only
```

O modo reduzido executa 1 cenário × 2 seeds × 4 algoritmos com 10 processos. O lote
principal e o reduzido não geram métricas individuais. `--verify-only` confere schema,
linha única, identidade, parâmetros e hashes de todos os workloads e resultados.
