#pragma once

#define DEBUG_CATCH_ANYTHING_AND_ABORT \
  catch (const std::exception &e) { \
    std::cerr << "exception: " << e.what() << std::endl; \
    abort(); \
  } catch (const int i) { \
    std::cerr << "exception: " << i << std::endl; \
    abort(); \
  } catch (const long l) { \
    std::cerr << "exception: " << l << std::endl; \
    abort(); \
  } catch (const char *p) { \
    std::cerr << "exception: " << p << std::endl; \
    abort(); \
  } catch (...) { \
    std::cerr << "unknown exception thrown" << std::endl; \
    abort(); \
  }
