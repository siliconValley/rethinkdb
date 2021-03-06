// Copyright 2010-2013 RethinkDB, all rights reserved.
#include "rdb_protocol/terms/terms.hpp"

#include "rdb_protocol/error.hpp"
#include "rdb_protocol/op.hpp"

namespace ql {

class case_term_t : public op_term_t {
public:
    case_term_t(compile_env_t *env, const protob_t<const Term> &term,
                const char *name, int (*_f)(int))
        : op_term_t(env, term, argspec_t(1)), name_(name), f(_f) { }
private:
    virtual counted_t<val_t> eval_impl(scope_env_t *env, UNUSED eval_flags_t flags) {
        std::string s = arg(env, 0)->as_str();
        std::transform(s.begin(), s.end(), s.begin(), f);
        return new_val(make_counted<const datum_t>(std::move(s)));
    }
    virtual const char *name() const { return name_; }

    const char *const name_;
    int (*const f)(int);
};

counted_t<term_t> make_upcase_term(compile_env_t *env,
                                   const protob_t<const Term> &term) {
    return make_counted<case_term_t>(env, term, "upcase", ::toupper);
}
counted_t<term_t> make_downcase_term(compile_env_t *env,
                                     const protob_t<const Term> &term) {
    return make_counted<case_term_t>(env, term, "downcase", ::tolower);
}

}
