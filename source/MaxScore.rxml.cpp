/*
  Copyright (c) 2022 Georg Hajdu Permission is hereby granted,
  free of charge, to any person obtaining a copy of this software
  and associated documentation files (the "Software"), to deal in
  the Software without restriction, including without limitation the
  rights to use, copy, modify, merge, publish, distribute,
  sublicense, and/or sell copies of the Software, and to permit
  persons to whom the Software is furnished to do so, subject to the
  following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
  HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
  WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.
*/

#include "ext.h"
#include "ext_obex.h"
// for postdictionary for debugging
// #include "jpatcher_utils.h"
#include "ext_critical.h"
#include "ext_dictobj.h"

#include "rapidxml.hpp"

// Workaround for an ancient bug that's never been fixed...
// https://stackoverflow.com/questions/14113923/rapidxml-print-header-has-undefined-methods
namespace rapidxml {
    namespace internal {
        template <class OutIt, class Ch>
        inline OutIt print_children(OutIt out, const xml_node<Ch>* node, int flags, int indent);

        template <class OutIt, class Ch>
        inline OutIt print_attributes(OutIt out, const xml_node<Ch>* node, int flags);

        template <class OutIt, class Ch>
        inline OutIt print_data_node(OutIt out, const xml_node<Ch>* node, int flags, int indent);

        template <class OutIt, class Ch>
        inline OutIt print_cdata_node(OutIt out, const xml_node<Ch>* node, int flags, int indent);

        template <class OutIt, class Ch>
        inline OutIt print_element_node(OutIt out, const xml_node<Ch>* node, int flags, int indent);

        template <class OutIt, class Ch>
        inline OutIt print_declaration_node(OutIt out, const xml_node<Ch>* node, int flags, int indent);

        template <class OutIt, class Ch>
        inline OutIt print_comment_node(OutIt out, const xml_node<Ch>* node, int flags, int indent);

        template <class OutIt, class Ch>
        inline OutIt print_doctype_node(OutIt out, const xml_node<Ch>* node, int flags, int indent);

        template <class OutIt, class Ch>
        inline OutIt print_pi_node(OutIt out, const xml_node<Ch>* node, int flags, int indent);
    }
}
#include "rapidxml_print.hpp"

#include <assert.h>
#include <iostream>
#include <string>
#include <sstream>

#define RXML_OUTLET_MAIN 0

void *rxml_class;

t_symbol *ps_dictionary, *ps_0, *ps_ordering, *ps_text;

using namespace rapidxml;

typedef struct _rxml
{
	t_object ob;
    void *outlets[1];
    t_critical lock;
    char *buf;
    size_t buflen, bufpos;
} rxml;

#ifdef RAPIDXML_NO_EXCEPTIONS
void rapidxml::parse_error_handler(const char *what, void *where)
{
    std::cout << "Parse error: " << what << "\n";
    // this function must not return
    std::abort();
}
#endif

extern "C" {
static void clearbuf(rxml *x);

static void rxml_anything(rxml *x,
                          const t_symbol * const s,
                          const long ac, const t_atom *av)
{
    assert(x);
    assert(s);
    const char * const str = s->s_name;
    assert(str);
    const size_t len = strlen(str);
    critical_enter(x->lock);
    size_t buflen = x->buflen;
    size_t bufpos = x->bufpos;
    critical_exit(x->lock);
    if(len + bufpos >= buflen)
    {
        buflen += 250000;
        critical_enter(x->lock);
        char *buf = (char *)realloc(x->buf, x->buflen);
        if(buf)
        {
            object_error((t_object *)x, "Out of memory!\n");
            x->buf = NULL;
            x->buflen = 0;
            x->bufpos = 0;
            critical_exit(x->lock);
            return;
        }
        x->buflen = buflen;
        x->buf = buf;
        critical_exit(x->lock);
    }
    critical_enter(x->lock);
    memcpy(x->buf + bufpos, str, len);
    x->bufpos += len;
    critical_exit(x->lock);

    
}

static void rxml_outputXML(const rxml * const x,
                           const xml_document<> * const doc)
{
    std::string s;
    print(std::back_inserter(s), *doc, 0);
    std::istringstream iss(s);
    std::string nl;
    while(std::getline(iss, nl, '\n'))
    {
        // std::cout << nl << "\n";
        // object_post((t_object *)x, "len = %d", nl.length());
        if(nl.length() == 0)
        {
            continue;
        }
        t_symbol *s = gensym(nl.c_str());
        outlet_anything(x->outlets[RXML_OUTLET_MAIN], s, 0, NULL);
    }
}

static xml_node<> *rxml_toXML(const rxml *x,
                              xml_document<> *doc,
                              const char * const elem,
                              const t_dictionary * const d);

static xml_node<> *rxml_toXML_entry(const rxml *x,
                                    xml_document<> *doc,
                                    const t_dictionary * const d,
                                    const char * const elem,
                                    const long nkeys,
                                    const t_symbol * const *keys,
                                    const long nordering,
                                    const t_atom * const ordering)
{
    assert(x);
    assert(doc);
    assert(d);
    assert(elem);
    //assert(keys);
    xml_node<> *node = doc->allocate_node(node_element, elem);
    if(nkeys && keys)
    {
        // attributes
        for(long i = 0; i < nkeys; ++i)
        {
            if(keys[i]->s_name && keys[i]->s_name[0] == '@')
            {
                t_atom *vals = NULL;
                long nvals = 0;
                t_max_err e = dictionary_getatoms(d, (t_symbol *)keys[i], &nvals, &vals);
                if(e)
                {
                    object_error((t_object *)x,
                                 "dictionary_getatom() produced "
                                 "an error: %d",
                                 e);
                    return NULL;
                }
                {
                    char *str = NULL;
                    long str_len = 0;
                    t_max_err e = atom_gettext(nvals, vals, &str_len, &str, 0);
                    if(e)
                    {
                        object_error((t_object *)x,
                                     "encountered an error while "
                                     "converting atoms to string");
                        return NULL;
                    }
                    if(!str || str_len == 0)
                    {
                        object_error((t_object *)x,
                                     "couldn't convert atoms to string");
                        return NULL;
                    }
                    xml_attribute<> *attr =
                        doc->allocate_attribute(keys[i]->s_name + 1,
                                                doc->allocate_string(str));
                    node->append_attribute(attr);
                    if(str)
                    {
                        sysmem_freeptr(str);
                    }
                }
            }
            else
            {
                // assert(0);
            }
        }
        // children
        if(nordering)
        {
            t_hashtab *ht = hashtab_new(0);
            for(long i = 0; i < nordering; ++i)
            {
                if(!dictionary_hasentry(d, atom_getsym(ordering + i)))
                {
                    object_error((t_object *)x,
                                 "found a symbol in .ordering that "
                                 "isn't in the dictionary");
                    return node;
                }
                t_atom val;
                t_max_err e = dictionary_getatom(d,
                                                 atom_getsym(ordering + i),
                                                 &val);
                if(e)
                {
                    object_error((t_object *)x,
                                 "dictionary_getatom() produced "
                                 "an error: %d",
                                 e);
                    return NULL;
                }
                if(atom_gettype(&val) == A_OBJ)
                {
                    t_atom_long count;
                    hashtab_lookuplong(ht, atom_getsym(ordering + i), &count);
                    char buf[16];
                    snprintf(buf, 16, "%ld", count);
                    hashtab_storelong(ht, atom_getsym(ordering + i), count + 1);
                    t_atom idxa;
                    dictionary_getatom((t_dictionary *)atom_getobj(&val),
                                       gensym(buf),
                                       &idxa);
                    if(atom_gettype(&idxa) != A_OBJ)
                    {
                        object_error((t_object *)x,
                                     "found something other than a dict.");
                        return NULL;                        
                    }
                    xml_node<> *nn =
                        rxml_toXML(x, doc, atom_getsym(ordering + i)->s_name,
                                   (t_dictionary *)atom_getobj(&idxa));
                    // (t_dictionary *)atom_getobj(&val));
                    node->append_node(nn);
                }
                else
                {
                    if(atom_gettype(&val) != A_SYM)
                    {
                        object_error((t_object *)x,
                                     "found an entry that is "
                                     "not a string");
                        return node;
                    }
                    xml_node<> *nn =
                        doc->allocate_node(node_element,
                                           atom_getsym(ordering + i)->s_name,
                                           atom_getsym(&val)->s_name);
                    node->append_node(nn);
                }
            }
            object_free((t_object *)ht);
        }
        else
        {
            for(long i = 0; i < nkeys; ++i)
            {
                if(keys[i]->s_name && keys[i]->s_name[0] != '@'
                   && strcmp(keys[i]->s_name, ".ordering"))
                {
                    t_atom val;
                    t_max_err e =
                        dictionary_getatom(d, (t_symbol *)keys[i], &val);
                    if(e)
                    {
                        object_error((t_object *)x,
                                     "dictionary_getatom() produced "
                                     "an error: %d",
                                     e);
                        return NULL;
                    }

                    if(atom_gettype(&val) == A_OBJ)
                    {
                        xml_node<> *nn =
                            rxml_toXML(x, doc, elem,//keys[i]->s_name,
                                       (t_dictionary *)atom_getobj(&val));
                        node->append_node(nn);
                    }
                    else
                    {
                        // This is a data node
                        if(atom_gettype(&val) != A_SYM)
                        {
                            object_error((t_object *)x,
                                         "found an entry that is "
                                         "not a string");
                            return node;
                        }
                        xml_node<> *nn =
                            doc->allocate_node(node_data,
                                               keys[i]->s_name,
                                               atom_getsym(&val)->s_name);
                        node->append_node(nn);
                    }

                }
                else
                {
                    // assert(0);
                }
            }
        }
        return node;
    }
    else
    {
        // assert(0);
    }
    return node;
}

static xml_node<> *rxml_toXML(const rxml *x,
                              xml_document<> *doc,
                              const char * const elem,
                              const t_dictionary * const d)
{
    assert(x);
    assert(doc);
    assert(elem);
    assert(d);
    t_symbol **keys = NULL;
    long nkeys = 0;
    t_atom *ordering = NULL;
    long nordering = 0;
    if(dictionary_hasentry(d, ps_ordering))
    {
        dictionary_getatoms(d, ps_ordering, &nordering, &ordering);
    }
    dictionary_getkeys(d, &nkeys, &keys);
    xml_node<> *n = rxml_toXML_entry(x, doc, d, elem, nkeys, keys,
                                     nordering, ordering);
    if(keys)
    {
        sysmem_freeptr(keys);
    }
    return n;
}

static void rxml_dictionary(rxml *x, const t_symbol * const s)
{
    assert(x);
    assert(s);
    xml_document<> doc;
    xml_node<> *node = NULL;
    t_dictionary *d = dictobj_findregistered_retain((t_symbol *)s);
    if(!d)
    {
        object_error((t_object *)x, "Couldn't find dictionary %s",
                     s->s_name);
        return;
    }
    {
        t_max_err e = MAX_ERR_NONE;
        t_symbol **keys = NULL;
        long nkeys = 0;
        t_atom val;
        e = dictionary_getkeys(d, &nkeys, &keys);
        if(e != MAX_ERR_NONE)
        {
            object_error((t_object *)x,
                         "dictionary_getkeys() returned an error: %d",
                         e);
            goto cleanup;
        }
        if(!nkeys || !keys)
        {
            object_error((t_object *)x, "dictionary is empty");
            goto cleanup;
        }
        if(nkeys > 1)
        {
            object_error((t_object *)x,
                         "malformed dictionary: "
                         "more than one root node");
            goto cleanup;
        }
        e = dictionary_getatom(d, keys[0], &val);
        if(e != MAX_ERR_NONE)
        {
            object_error((t_object *)x,
                         "dictionary_getatom() returned an error: %d",
                         e);
            goto cleanup;
        }
        if(atom_gettype(&val) != A_OBJ)
        {
            object_error((t_object *)x,
                         "data for the root node is not a dict");
            goto cleanup;
        }
        node = rxml_toXML(x, &doc, 
                          keys[0]->s_name,
                          (t_dictionary *)atom_getobj(&val));
        if(node)
        {
        	doc.append_node(node);
        }
        rxml_outputXML(x, &doc);
        if(keys)
        {
            sysmem_freeptr(keys);
        }
    }
    
cleanup:
    dictobj_release(d);
    doc.clear();
}

static void rxml_toJSON(rxml *x, const xml_node<> *node,
                        t_dictionary *d)
{
    assert(node);
    assert(d);
    const node_type t = node->type();
    switch(t)
    {
    case node_element:
    {
        t_dictionary *thiselem = dictionary_new();
        t_symbol *thiselem_name = gensym(node->name());
        if(dictionary_hasentry(d, thiselem_name))
        {
            t_dictionary *parent = NULL;
            t_max_err e = dictionary_getdictionary(d, thiselem_name,
                                                   (t_object **)&parent);
            if(e)
            {
                object_error((t_object *)x,
                             "Error converting to JSON");
                return;
            }
            long nkeys = dictionary_getentrycount(parent);
            char k[32];
            snprintf(k, 32, "%ld", nkeys);
            dictionary_appenddictionary(parent, gensym(k),
                                        (t_object *)thiselem);
        }
        else
        {
            t_dictionary *dd = dictionary_new();
            dictionary_appenddictionary(d, thiselem_name,
                                        (t_object *)dd);
            dictionary_appenddictionary(dd, ps_0,
                                        (t_object *)thiselem);
        }
        
        for(const xml_attribute<> *a = node->first_attribute();
            a;
            a = a->next_attribute())
        {
            size_t n = strlen(a->name());
            char key[n + 2];
            snprintf(key, n + 2, "@%s", a->name());
            {
                t_atom *vals = NULL;
                long nvals = 0;
                t_max_err e = atom_setparse(&nvals, &vals, a->value());
                if(e)
                {
                    object_error((t_object *)x,
                                 "encountered an error parsing string "
                                 "to atom array");
                    return;
                }
                if(vals && nvals)
                {
                    if(nvals == 1)
                    {
                        dictionary_appendatom(thiselem,
                                              gensym(key),
                                              vals);
                    }
                    else
                    {
                        dictionary_appendatoms(thiselem, gensym(key),
                                               nvals, vals);
                    }
                    sysmem_freeptr(vals);
                }
            }
        }

        long i = 0;
        for(const xml_node<> *n = node->first_node();
            n;
            n = n->next_sibling())
        {
            if(n->type() == node_element)
            {
                ++i;
            }
        }

        {
            t_atom ordering[i];
            i = 0;
            for(const xml_node<> *n = node->first_node();
                n;
                n = n->next_sibling())
            {
                if(n->type() == node_element)
                {
                    atom_setsym(ordering + i, gensym(n->name()));
                    ++i;
                }
                rxml_toJSON(x, n, thiselem);
            }
            if(i)
            {
                dictionary_appendatoms(thiselem, ps_ordering,
                                       i, ordering);
            }
        }
    }
    break;
    case node_data:
    {
        dictionary_appendsym(d,
                             ps_text,
                             gensym(node->value()));
    }
    break;
    default:
        object_error((t_object *)x,
                     "Encountered unexpected node type: %d",
                     t);
        return;
    }
}

static void rxml_bang(rxml *x)
{
    critical_enter(x->lock);
    size_t bufpos = x->bufpos;
    critical_exit(x->lock);
    if(!bufpos)
    {
        object_error((t_object *)x, "no text to process");
        return;
    }
    char *buf = (char *)malloc(bufpos + 1);
    if(!buf)
    {
        object_error((t_object *)x,
                     "Couldn't allocate memory for temporary buffer");
        return;
    }
    critical_enter(x->lock);
    memcpy(buf, x->buf, bufpos + 1);
    critical_exit(x->lock);
    buf[bufpos] = 0;
    
    xml_document<> doc;

    // RAPIDXML_NO_EXCEPTIONS is defined in the Xcode project when
    // building in debug mode, which will cause an assertion to
    // fire in the case of an error.
#ifndef RAPIDXML_NO_EXCEPTIONS
    try{
        doc.parse<0>(buf);
    }
    catch (const std::runtime_error& e)
    {
        //std::cerr << "Runtime error was: " << e.what() << std::endl;
        object_error((t_object *)x, "Runtime error: %s", e.what());
        clearbuf(x);
        return;
    }
    catch (const rapidxml::parse_error& e)
    {
        object_error((t_object *)x, "Parse error: %s", e.what());
        //std::cerr << "Parse error was: " << e.what() << std::endl;
        clearbuf(x);
        return;
    }
    catch (const std::exception& e)
    {
        object_error((t_object *)x, "Error: %s", e.what());
        //std::cerr << "Error was: " << e.what() << std::endl;
        clearbuf(x);
        return;
    }
    catch (...)
    {
        object_error((t_object *)x, "Unknown error");
        // std::cerr << "An unknown error occurred." << std::endl;
        clearbuf(x);
        return;
    }
#else
    doc.parse<0>(buf);
#endif
    
    xml_node<> *root = doc.first_node();
    if(!root)
    {
        object_error((t_object *)x, "No root!");
        goto cleanup;
    }
    else
    {
        t_dictionary *rd = dictionary_new();
        rxml_toJSON(x, root, rd);
        {
			// the root node is special--the file cannot contain
            // multiple copies of it, so it shouldn't have an index
            t_dictionary *d = NULL;
            t_max_err e = dictionary_getdictionary(rd,
                                                   gensym(root->name()),
                                                   (t_object **)&d);
            if(e)
            {
                object_error((t_object *)x,
                             "error converting to dict: "
                             "no root node was created (%d)",
                             e);
                goto cleanup;
            }
            t_dictionary *dd = NULL;
            e = dictionary_getdictionary(d,
                                         ps_0,
                                         (t_object **)&dd);
            if(e)
            {
                object_error((t_object *)x,
                             "error converting to dict (%d)",
                             e);
                goto cleanup;
            }
            dictionary_chuckentry(rd, gensym(root->name()));
            dictionary_chuckentry(d, ps_0);
            object_free((t_object *)d);
            dictionary_appenddictionary(rd,
                                        gensym(root->name()),
                                        (t_object *)dd);
        }
        t_symbol *name = NULL;
        t_dictionary *dd = dictobj_register(rd, &name);
        if(!dd || !name)
        {
            object_error((t_object *)x, "Couldn't register dict");
            goto cleanup;
        }
        t_atom out;
        atom_setsym(&out, name);
        outlet_anything(x->outlets[RXML_OUTLET_MAIN],
                        ps_dictionary, 1, &out);
        dictobj_release(dd);
    }
cleanup:
    if(buf)
    {
        free(buf);
    }
    critical_enter(x->lock);
    memset(x->buf, 0, x->bufpos);
    x->bufpos = 0;
    doc.clear();
    critical_exit(x->lock);
}

static void rxml_clear(rxml *x)
{
    clearbuf(x);
}

__attribute__((used))
static void clearbuf(rxml *x)
{
    assert(x);
    if(x->buf)
    {
        memset(x->buf, 0, x->buflen);
    }
    x->bufpos = 0;
}

static void rxml_free(rxml *x)
{
    critical_free(x->lock);
    if(x->buf)
    {
        free(x->buf);
    }
}

static void rxml_assist(rxml *x, void *b, long m, long a, char *s)
{
	if(m==1)
    {
        // inlets
		switch(a){
		case 0:
            sprintf(s, "Inlet");
            break;
		}
	}
	else if(m==2)
    {
		switch(a)
        {
		case 0:
            sprintf(s, "Outlet");
            break;
		}
	}
}

static void *rxml_new(t_symbol *sym, long ac, t_atom *av)
{
    rxml *x = (rxml *)object_alloc((t_class *)rxml_class);
    if(x == NULL)
    {
        return NULL;
    }
    critical_new(&(x->lock));
    x->outlets[RXML_OUTLET_MAIN] = outlet_new((t_object *)x, NULL);
    x->buf = (char *)calloc(1000000, 1);
    if(!x->buf)
    {
        object_error((t_object *)x, "Couldn't allocate memory");
        return NULL;
    }
    x->buflen = 1000000;
    x->bufpos = 0;
	return x;
}

void ext_main(void *r)
{
	t_class *c = class_new("MaxScore.rxml",
                           (method)rxml_new,
                           (method)rxml_free,
                           (short)sizeof(rxml),
                           0L, A_GIMME, 0);

    class_addmethod(c, (method)rxml_anything, "anything", A_GIMME, 0);
    class_addmethod(c, (method)rxml_bang, "bang", 0);
    class_addmethod(c, (method)rxml_clear, "clear", 0);
    class_addmethod(c, (method)rxml_dictionary, "dictionary", A_SYM, 0);
	class_addmethod(c, (method)rxml_assist,	"assist", A_CANT, 0);

	class_register(CLASS_BOX, c);
	rxml_class = c;
    ps_dictionary = gensym("dictionary");
    ps_0 = gensym("0");
    ps_ordering = gensym(".ordering");
    ps_text = gensym(".text");
}

} // extern "C"
