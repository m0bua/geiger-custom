"""Storage regressions for the new log engine (compiled ARM/Unicorn)."""
import sys
sys.dont_write_bytecode = True
from test_log_engine import main

if __name__ == '__main__':
    main()
