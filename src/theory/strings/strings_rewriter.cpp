/******************************************************************************
 * Top contributors (to current version):
 *   Andrew Reynolds, Andres Noetzli, Aina Niemetz
 *
 * This file is part of the cvc5 project.
 *
 * Copyright (c) 2009-2025 by the authors listed in the file AUTHORS
 * in the top-level source directory and their institutional affiliations.
 * All rights reserved.  See the file COPYING in the top-level source
 * directory for licensing information.
 * ****************************************************************************
 *
 * Implementation of rewrite rules for string-specific operators in
 * theory of strings.
 */

#include "theory/strings/strings_rewriter.h"

#include "expr/node_builder.h"
#include "theory/strings/theory_strings_utils.h"
#include "util/rational.h"
#include "util/string.h"

using namespace cvc5::internal::kind;

namespace cvc5::internal {
namespace theory {
namespace strings {

StringsRewriter::StringsRewriter(NodeManager* nm,
                                 ArithEntail& ae,
                                 StringsEntail& se,
                                 HistogramStat<Rewrite>* statistics,
                                 uint32_t alphaCard)
    : SequencesRewriter(nm, ae, se, statistics), d_alphaCard(alphaCard)
{
}

RewriteResponse StringsRewriter::postRewrite(TNode node)
{
  Trace("strings-postrewrite")
      << "Strings::StringsRewriter::postRewrite start " << node << std::endl;

  Node retNode = node;
  Kind nk = node.getKind();
  if (nk == Kind::STRING_LT)
  {
    retNode = rewriteStringLt(node);
  }
  else if (nk == Kind::STRING_LEQ)
  {
    retNode = rewriteStringLeq(node);
  }
  else if (nk == Kind::STRING_TO_LOWER || nk == Kind::STRING_TO_UPPER)
  {
    retNode = rewriteStrConvert(node);
  }
  else if (nk == Kind::STRING_IS_DIGIT)
  {
    retNode = rewriteStringIsDigit(node);
  }
  else if (nk == Kind::STRING_ITOS)
  {
    retNode = rewriteIntToStr(node);
  }
  else if (nk == Kind::STRING_STOI)
  {
    retNode = rewriteStrToInt(node);
  }
  else if (nk == Kind::STRING_TO_CODE)
  {
    retNode = rewriteStringToCode(node);
  }
  else if (nk == Kind::STRING_FROM_CODE)
  {
    retNode = rewriteStringFromCode(node);
  }
  else if (nk == Kind::STRING_UNIT)
  {
    retNode = rewriteStringUnit(node);
  }
  else
  {
    return SequencesRewriter::postRewrite(node);
  }

  Trace("strings-postrewrite")
      << "Strings::StringsRewriter::postRewrite returning " << retNode
      << std::endl;
  if (node != retNode)
  {
    Trace("strings-rewrite-debug") << "Strings::StringsRewriter::postRewrite "
                                   << node << " to " << retNode << std::endl;
    return RewriteResponse(REWRITE_AGAIN_FULL, retNode);
  }
  return RewriteResponse(REWRITE_DONE, retNode);
}

Node StringsRewriter::rewriteStrToInt(Node node)
{
  Assert(node.getKind() == Kind::STRING_STOI);
  NodeManager* nm = nodeManager();
  if (node[0].isConst())
  {
    Node ret;
    String s = node[0].getConst<String>();
    if (s.isNumber())
    {
      ret = nm->mkConstInt(s.toNumber());
    }
    else
    {
      ret = nm->mkConstInt(Rational(-1));
    }
    return returnRewrite(node, ret, Rewrite::STOI_EVAL);
  }
  else if (node[0].getKind() == Kind::STRING_CONCAT)
  {
    for (TNode nc : node[0])
    {
      if (nc.isConst())
      {
        String t = nc.getConst<String>();
        if (!t.isNumber())
        {
          Node ret = nm->mkConstInt(Rational(-1));
          return returnRewrite(node, ret, Rewrite::STOI_CONCAT_NONNUM);
        }
      }
    }
  }
  return node;
}

Node StringsRewriter::rewriteIntToStr(Node node)
{
  Assert(node.getKind() == Kind::STRING_ITOS);
  NodeManager* nm = nodeManager();
  if (node[0].isConst())
  {
    Node ret;
    if (node[0].getConst<Rational>().sgn() == -1)
    {
      ret = nm->mkConst(String(""));
    }
    else
    {
      std::string stmp = node[0].getConst<Rational>().getNumerator().toString();
      Assert(stmp[0] != '-');
      ret = nm->mkConst(String(stmp));
    }
    return returnRewrite(node, ret, Rewrite::ITOS_EVAL);
  }
  return node;
}

Node StringsRewriter::rewriteStrConvert(Node node)
{
  Kind nk = node.getKind();
  Assert(nk == Kind::STRING_TO_LOWER || nk == Kind::STRING_TO_UPPER);
  NodeManager* nm = nodeManager();
  if (node[0].isConst())
  {
    std::vector<unsigned> nvec = node[0].getConst<String>().getVec();
    for (unsigned i = 0, nvsize = nvec.size(); i < nvsize; i++)
    {
      unsigned newChar = nvec[i];
      // transform it
      // upper 65 ... 90
      // lower 97 ... 122
      if (nk == Kind::STRING_TO_UPPER)
      {
        if (newChar >= 97 && newChar <= 122)
        {
          newChar = newChar - 32;
        }
      }
      else if (nk == Kind::STRING_TO_LOWER)
      {
        if (newChar >= 65 && newChar <= 90)
        {
          newChar = newChar + 32;
        }
      }
      nvec[i] = newChar;
    }
    Node retNode = nm->mkConst(String(nvec));
    return returnRewrite(node, retNode, Rewrite::STR_CONV_CONST);
  }
  else if (node[0].getKind() == Kind::STRING_CONCAT)
  {
    NodeBuilder concatBuilder(nm, Kind::STRING_CONCAT);
    for (const Node& nc : node[0])
    {
      concatBuilder << nm->mkNode(nk, nc);
    }
    // to_lower( x1 ++ x2 ) --> to_lower( x1 ) ++ to_lower( x2 )
    Node retNode = concatBuilder.constructNode();
    return returnRewrite(node, retNode, Rewrite::STR_CONV_MINSCOPE_CONCAT);
  }
  else if (node[0].getKind() == Kind::STRING_TO_LOWER
           || node[0].getKind() == Kind::STRING_TO_UPPER)
  {
    // to_lower( to_lower( x ) ) --> to_lower( x )
    // to_lower( toupper( x ) ) --> to_lower( x )
    Node retNode = nm->mkNode(nk, node[0][0]);
    return returnRewrite(node, retNode, Rewrite::STR_CONV_IDEM);
  }
  else if (node[0].getKind() == Kind::STRING_ITOS)
  {
    // to_lower( str.from.int( x ) ) --> str.from.int( x )
    return returnRewrite(node, node[0], Rewrite::STR_CONV_ITOS);
  }
  return node;
}

Node StringsRewriter::rewriteStringLt(Node n)
{
  Assert(n.getKind() == Kind::STRING_LT);
  NodeManager* nm = nodeManager();
  // eliminate s < t ---> s != t AND s <= t
  Node retNode = nm->mkNode(Kind::AND,
                            n[0].eqNode(n[1]).negate(),
                            nm->mkNode(Kind::STRING_LEQ, n[0], n[1]));
  return returnRewrite(n, retNode, Rewrite::STR_LT_ELIM);
}

Node StringsRewriter::rewriteStringLeq(Node n)
{
  Assert(n.getKind() == Kind::STRING_LEQ);
  NodeManager* nm = nodeManager();
  if (n[0] == n[1])
  {
    Node ret = nm->mkConst(true);
    return returnRewrite(n, ret, Rewrite::STR_LEQ_ID);
  }
  if (n[0].isConst() && n[1].isConst())
  {
    String s = n[0].getConst<String>();
    String t = n[1].getConst<String>();
    Node ret = nm->mkConst(s.isLeq(t));
    return returnRewrite(n, ret, Rewrite::STR_LEQ_EVAL);
  }
  // empty strings
  for (unsigned i = 0; i < 2; i++)
  {
    if (n[i].isConst() && n[i].getConst<String>().empty())
    {
      Node ret = i == 0 ? nm->mkConst(true) : n[0].eqNode(n[1]);
      return returnRewrite(n, ret, Rewrite::STR_LEQ_EMPTY);
    }
  }

  std::vector<Node> n1;
  utils::getConcat(n[0], n1);
  std::vector<Node> n2;
  utils::getConcat(n[1], n2);
  Assert(!n1.empty() && !n2.empty());

  // constant prefixes
  if (n1[0].isConst() && n2[0].isConst() && n1[0] != n2[0])
  {
    String s = n1[0].getConst<String>();
    String t = n2[0].getConst<String>();
    size_t prefixLen = std::min(s.size(), t.size());
    s = s.prefix(prefixLen);
    t = t.prefix(prefixLen);
    // if the prefixes are not the same, then we can already decide the outcome
    if (s != t)
    {
      Node ret = nm->mkConst(s.isLeq(t));
      return returnRewrite(n, ret, Rewrite::STR_LEQ_CPREFIX);
    }
  }
  return n;
}

Node StringsRewriter::rewriteStringFromCode(Node n)
{
  Assert(n.getKind() == Kind::STRING_FROM_CODE);
  NodeManager* nm = nodeManager();

  if (n[0].isConst())
  {
    Integer i = n[0].getConst<Rational>().getNumerator();
    Node ret;
    if (i >= 0 && i < d_alphaCard)
    {
      std::vector<unsigned> svec = {i.toUnsignedInt()};
      ret = nm->mkConst(String(svec));
    }
    else
    {
      ret = nm->mkConst(String(""));
    }
    return returnRewrite(n, ret, Rewrite::FROM_CODE_EVAL);
  }
  return n;
}

Node StringsRewriter::rewriteStringToCode(Node n)
{
  Assert(n.getKind() == Kind::STRING_TO_CODE);
  if (n[0].isConst())
  {
    NodeManager* nm = nodeManager();
    String s = n[0].getConst<String>();
    Node ret;
    if (s.size() == 1)
    {
      std::vector<unsigned> vec = s.getVec();
      Assert(vec.size() == 1);
      ret = nm->mkConstInt(Rational(vec[0]));
    }
    else
    {
      ret = nm->mkConstInt(Rational(-1));
    }
    return returnRewrite(n, ret, Rewrite::TO_CODE_EVAL);
  }
  return n;
}

Node StringsRewriter::rewriteStringIsDigit(Node n)
{
  Assert(n.getKind() == Kind::STRING_IS_DIGIT);
  NodeManager* nm = nodeManager();
  // eliminate str.is_digit(s) ----> 48 <= str.to_code(s) <= 57
  Node t = nm->mkNode(Kind::STRING_TO_CODE, n[0]);
  Node retNode =
      nm->mkNode(Kind::AND,
                 nm->mkNode(Kind::LEQ, nm->mkConstInt(Rational(48)), t),
                 nm->mkNode(Kind::LEQ, t, nm->mkConstInt(Rational(57))));
  return returnRewrite(n, retNode, Rewrite::IS_DIGIT_ELIM);
}

Node StringsRewriter::rewriteStringUnit(Node n)
{
  Assert(n.getKind() == Kind::STRING_UNIT);
  NodeManager* nm = nodeManager();
  if (n[0].isConst())
  {
    Integer i = n[0].getConst<Rational>().getNumerator();
    Node ret;
    if (i >= 0 && i < d_alphaCard)
    {
      std::vector<unsigned> svec = {i.toUnsignedInt()};
      ret = nm->mkConst(String(svec));
      return returnRewrite(n, ret, Rewrite::SEQ_UNIT_EVAL);
    }
  }
  return n;
}

}  // namespace strings
}  // namespace theory
}  // namespace cvc5::internal
