/*
 *  Scilab ( http://www.scilab.org/ ) - This file is part of Scilab
 *  Copyright (C) 2014 - Scilab Enterprises - Antoine ELIAS
 *
 *  This file must be used under the terms of the CeCILL.
 *  This source file is licensed as described in the file COPYING, which
 *  you should have received as part of this distribution.  The terms
 *  are also available at
 *  http://www.cecill.info/licences/Licence_CeCILL_V2-en.txt
 *
 */

//file included in runvisitor.cpp
namespace ast {

template<class T>
void RunVisitorT<T>::visitprivate(const CallExp &e)
{
    e.getName().accept(*this);
    types::InternalType* pIT = getResult();

    if (pIT != NULL)
    {
        //function call
        types::typed_list out;
        types::typed_list in;
        types::optional_list opt;

        int iRetCount = getExpectedSize();
        int iSaveExpectedSize = iRetCount;

        // manage case [a,b]=foo() where foo is defined as a=foo()
        if (pIT->getInvokeNbOut() != -1 && pIT->getInvokeNbOut() < iRetCount)
        {
            clearResult();
            std::wostringstream os;
            os << _W("Wrong number of output arguments.\n") << std::endl;
            throw ast::ScilabError(os.str(), 999, e.getLocation());
        }

        //get function arguments
        exps_t args = e.getArgs();
        for (auto arg : args)
        {
            if (arg->isAssignExp())
            {
                AssignExp* pAssign = static_cast<AssignExp*>(arg);
                //optional parameter
                Exp* pL = &pAssign->getLeftExp();
                if (!pL->isSimpleVar())
                {
                    clearResult();
                    cleanOpt(opt);
                    cleanIn(in, out);

                    std::wostringstream os;
                    os << _W("left side of optional parameter must be a variable") << std::endl;
                    throw ast::ScilabError(os.str(), 999, e.getLocation());
                }

                SimpleVar* pVar = pL->getAs<SimpleVar>();
                Exp* pR = &pAssign->getRightExp();
                pR->accept(*this);
                InternalType* pITR = getResult();
                // IncreaseRef to protect opt argument of scope_end delete
                // It will be deleted by clear_opt
                pITR->IncreaseRef();

                if (pIT->hasInvokeOption())
                {
                    opt.push_back(std::pair<std::wstring, InternalType*>(pVar->getSymbol().getName(), pITR));
                    //in case of macro/macrofile, we have to shift input param
                    //so add NULL item in in list to keep initial order
                    if (pIT->isMacro() || pIT->isMacroFile())
                    {
                        in.push_back(NULL);
                    }
                }
                else
                {
                    in.push_back(pITR);
                }

                clearResult();
                continue;
            }

            int iSize = getExpectedSize();
            setExpectedSize(-1);
            arg->accept(*this);
            setExpectedSize(iSize);

            if (getResult() == NULL)
            {
                //special case for empty extraction of list ( list()(:) )
                continue;
            }

            InternalType * pITArg = getResult();
            if (pITArg->isImplicitList())
            {
                types::ImplicitList* pIL = pITArg->getAs<types::ImplicitList>();
                if (pIL->isComputable())
                {
                    setResult(pIL->extractFullMatrix());
                    pITArg->killMe();
                }
            }

            if (isSingleResult())
            {
                in.push_back(getResult());
                getResult()->IncreaseRef();
                clearResult();
            }
            else
            {
                for (int i = 0 ; i < getResultSize() ; i++)
                {
                    InternalType * pITArg = getResult(i);
                    pITArg->IncreaseRef();
                    in.push_back(pITArg);
                }

                clearResult();
            }
        }

        try
        {
            // Extraction with a List in input argument.
            // This extraction must be a recursive extract.
            int iLoopSize = 1;
            types::List* pListArg = NULL;
            if (pIT->isCallable() == false && in.size() == 1 && in[0]->isList())
            {
                pListArg = in[0]->getAs<types::List>();
                iLoopSize = pListArg->getSize();
                cleanOpt(opt);
            }

            setExpectedSize(iSaveExpectedSize);
            iRetCount = std::max(1, iRetCount);

            for (int i = 0; i < iLoopSize; i++)
            {
                if (pListArg)
                {
                    in[0] = pListArg->get(i);

                    if (in[0]->isList())
                    {
                        if (pIT->isCallable())
                        {
                            // list used like "varargin"
                            types::List* pLFuncArgs = in[0]->getAs<types::List>();
                            types::typed_list input;
                            for (int j = 0; j < pLFuncArgs->getSize(); j++)
                            {
                                input.push_back(pLFuncArgs->get(j));
                                input.back()->IncreaseRef();
                            }

                            in = input;
                        }
                        else
                        {
                            pListArg->DecreaseRef();
                            pListArg->killMe();

                            std::wostringstream os;
                            os << _W("Invalid index.\n");
                            throw ast::ScilabError(os.str(), 999, e.getFirstLocation());
                        }
                    }
                    else
                    {
                        in[0]->IncreaseRef();
                    }
                }

                bool ret = false;
                if(pIT->isInvokable() == false)
                {
                    // call overload
                    ret = Overload::call(L"%" + pIT->getShortTypeStr() + L"_e", in, iRetCount, out, this);
                }
                else
                {
                    ret = pIT->invoke(in, opt, iRetCount, out, *this, e);
                    if(ret == false && pIT->isUserType())
                    {
                        // call overload
                        ret = Overload::call(L"%" + pIT->getShortTypeStr() + L"_e", in, iRetCount, out, this);
                    }
                }

                if (ret)
                {
                    if (iSaveExpectedSize != -1 && iSaveExpectedSize > out.size())
                    {
                        std::wostringstream os;
                        os << _W("bad lhs, expected : ") << iRetCount << _W(" returned : ") << out.size() << std::endl;
                        throw ScilabError(os.str(), 999, e.getLocation());
                    }

                    setExpectedSize(iSaveExpectedSize);
                    setResult(out);
                    cleanIn(in, out);
                    cleanOpt(opt);

                    // In case a.b(), getResult contain pIT ("b").
                    // If out == pIT, do not delete it.
                    if (getResult() != pIT)
                    {
                        // protect element of out in case where
                        // out contain elements of pIT
                        for (int i = 0; i < out.size(); i++)
                        {
                            out[i]->IncreaseRef();
                        }

                        pIT->killMe();

                        // unprotect
                        for (int i = 0; i < out.size(); i++)
                        {
                            out[i]->DecreaseRef();
                        }
                    }

                    if (pListArg && i + 1 != iLoopSize)
                    {
                        pIT = out[0];
                        out.clear();
                        setResult(NULL);
                    }
                }
                else
                {
                    std::wostringstream os;
                    os << _W("Invalid index.\n");
                    throw ast::ScilabError(os.str(), 999, e.getFirstLocation());
                }
            }

            if (pListArg)
            {
                pListArg->DecreaseRef();
                pListArg->killMe();
            }
        }
        catch (ScilabMessage & sm)
        {
            setExpectedSize(iSaveExpectedSize);
            if(pIT != getResult())
            {
                pIT->killMe();
            }

            clearResult();
            cleanInOut(in, out);
            cleanOpt(opt);

            if (pIT->isCallable())
            {
                Callable *pCall = pIT->getAs<Callable>();
                if (ConfigVariable::getLastErrorFunction() == L"")
                {
                    ConfigVariable::setLastErrorFunction(pCall->getName());
                }

                if (pCall->isMacro() || pCall->isMacroFile())
                {
                    wchar_t szError[bsiz];
                    os_swprintf(szError, bsiz, _W("at line % 5d of function %ls called by :\n").c_str(), sm.GetErrorLocation().first_line, pCall->getName().c_str());
                    throw ScilabMessage(szError);
                }
            }

            throw sm;
        }
        catch (InternalAbort & ia)
        {
            setExpectedSize(iSaveExpectedSize);
            if(pIT != getResult())
            {
                pIT->killMe();
            }

            clearResult();
            cleanInOut(in, out);
            cleanOpt(opt);

            throw ia;
        }
        catch (ScilabError & se)
        {
            setExpectedSize(iSaveExpectedSize);
            if(pIT != getResult())
            {
                pIT->killMe();
            }

            clearResult();
            cleanInOut(in, out);
            cleanOpt(opt);

            throw se;
        }
    }
}

template<class T>
void RunVisitorT<T>::visitprivate(const CellCallExp &e)
{
    //get head
    T execMeCell;
    e.getName().accept(execMeCell);

    if (execMeCell.getResult() != NULL)
    {
        //a{xxx} with a variable, extraction
        types::InternalType *pIT = NULL;

        pIT = execMeCell.getResult();

        if (pIT)
        {

            if (pIT->isCell() == false)
            {
                throw ast::ScilabError(_W("[error] Cell contents reference from a non-cell array object.\n"), 999, e.getFirstLocation());
            }
            //Create list of indexes
            ast::exps_t exps = e.getArgs();
            types::typed_list *pArgs = GetArgumentList(exps);

            types::List* pList = pIT->getAs<types::Cell>()->extractCell(pArgs);

            if (pList == NULL)
            {
                delete pArgs;
                std::wostringstream os;
                os << _W("inconsistent row/column dimensions\n");
                //os << ((*e.args_get().begin())->getLocation()).getLocationString() << std::endl;
                throw ast::ScilabError(os.str(), 999, e.getFirstLocation());
            }

            if (pList->getSize() == 1)
            {
                InternalType* ret = pList->get(0);
                setResult(ret);

                ret->IncreaseRef();
                pList->killMe();
                ret->DecreaseRef();
            }
            else
            {
                setResult(pList);
            }


            //clean pArgs return by GetArgumentList
            for (int iArg = 0 ; iArg < (int)pArgs->size() ; iArg++)
            {
                (*pArgs)[iArg]->killMe();
            }
            delete pArgs;
        }
    }
    else
    {
        //result == NULL ,variable doesn't exist :(
        // Sould never be in this case
        // In worst case variable pointing to function does not exists
        // visitprivate(SimpleVar) will throw the right exception.
    }
}

} /* namespace ast */