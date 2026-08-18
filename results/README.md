# Resultados

- `raw/`: saídas grandes de execução. Fica fora do Git e pode ser regenerado por cenário, seed e commit.
- `consolidated/`: tabelas consolidadas usadas no artigo. Deve ser versionado.
- `figures/`: gráficos finais usados no artigo e apresentação. Deve ser versionado.

Registre no manifesto experimental os comandos, seeds, configurações, hash do commit e hashes das cargas/resultados para manter reprodutibilidade.

Os arquivos brutos de uma execução são CSV. O resultado agregado possui uma linha identificada por `run_id` e `workload_sha256`; métricas por processo são opcionais. Arquivos com extensão `.json` ficam reservados ao manifesto experimental da ISSUE-13.

## Executor experimental

`raw/` contém artefatos reproduzíveis, porém potencialmente grandes, e é ignorado pelo
Git. Cada execução de `scripts/run_experiment.py` cria um diretório identificado por
`experiment_id`, com este formato:

```text
raw/<experiment_id>/
├── manifest.json
├── workloads/<cenario>_seed_<seed>.csv
└── runs/<cenario>_<algoritmo>_seed_<seed>.csv
```

O manifesto é a fonte de rastreabilidade e registra identidade do código/configuração,
hashes SHA-256, comandos, horários, tentativas, falhas e causas de reexecução. Não copie
arquivos entre experimentos: alterações de commit, binário ou configuração exigem
outro identificador.

O resumo é atualizado durante o lote. Uma interrupção por `Ctrl+C` marca a tentativa
corrente como `interrupted`; execute novamente com o mesmo `experiment_id` para retomar
sem substituir resultados válidos. `make clean` remove `raw/` e não deve anteceder uma
retomada.

Resultados estatísticos pequenos e revisados pertencem a `consolidated/`; gráficos
finais pertencem a `figures/`.

## Consolidacao estatistica

Depois que `results/raw/main/manifest.json` registrar as 1.600 execucoes validas,
regenere todos os artefatos com um comando:

```sh
make graphs
```

O Make cria `.venv/` quando necessario, instala nela a dependencia direta fixada em `requirements-analysis.txt` e executa o analisador com `.venv/bin/python`. O arquivo de controle da instalacao so e atualizado depois que o `pip` termina com sucesso; alterar o arquivo de requisitos provoca uma nova instalacao automaticamente.

O comando revalida o manifesto, os hashes e cada CSV antes de publicar qualquer arquivo. Cada uma das 16 combinacoes de cenario e algoritmo deve conter exatamente as seeds 1 a 100; ausencias, duplicatas, `NaN`, infinito e valores impossiveis interrompem a consolidacao. O commit registrado tambem deve conter a implementacao do PDBH; lotes anteriores sao preservados, mas nao podem gerar resultados finais.
Para analisar outro ID sem renomear seus dados, use `make graphs EXPERIMENT_ID=<id>`. A saida e `consolidated/summary.csv`, com media, desvio padrao amostral (`n - 1`) e IC95%, alem de tres PNGs a 300 DPI em `figures/`.
