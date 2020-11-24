/* Copyright 2013-2020 Axel Huebl, Heiko Burau, Rene Widera, Felix Schmitt,
 *                     Richard Pausch, Benjamin Worpitz, Sergei Bastrakov
 *
 * This file is part of PIConGPU.
 *
 * PIConGPU is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PIConGPU is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PIConGPU.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "picongpu/simulation_defines.hpp"
#include "picongpu/fields/EMFieldBase.hpp"
#include "picongpu/fields/MaxwellSolver/Solvers.hpp"
#include "picongpu/particles/traits/GetInterpolation.hpp"
#include "picongpu/traits/GetMargin.hpp"
#include "picongpu/traits/SIBaseUnits.hpp"
#include "picongpu/particles/traits/GetMarginPusher.hpp"

#include <pmacc/dataManagement/DataConnector.hpp>
#include <pmacc/dimensions/SuperCellDescription.hpp>
#include <pmacc/eventSystem/EventSystem.hpp>
#include <pmacc/mappings/kernel/AreaMapping.hpp>
#include <pmacc/mappings/kernel/ExchangeMapping.hpp>
#include <pmacc/math/Vector.hpp>
#include <pmacc/memory/buffers/GridBuffer.hpp>
#include <pmacc/particles/traits/FilterByFlag.hpp>

#include <boost/mpl/accumulate.hpp>

#include <memory>
#include <type_traits>


namespace picongpu
{
    namespace fields
    {
        template<CommunicationTag T_tag>
        EMFieldBase::EMFieldBase(
            MappingDesc const& cellDescription,
            pmacc::SimulationDataId const& id,
            std::integral_constant<CommunicationTag, T_tag>)
            : SimulationFieldHelper<MappingDesc>(cellDescription)
            , id(id)
        {
            buffer = std::make_unique<Buffer>(cellDescription.getGridLayout());

            using VectorSpeciesWithInterpolation =
                typename pmacc::particles::traits::FilterByFlag<VectorAllSpecies, interpolation<>>::type;
            using LowerMarginInterpolation = bmpl::accumulate<
                VectorSpeciesWithInterpolation,
                typename pmacc::math::CT::make_Int<simDim, 0>::type,
                pmacc::math::CT::max<bmpl::_1, GetLowerMargin<GetInterpolation<bmpl::_2>>>>::type;
            using UpperMarginInterpolation = bmpl::accumulate<
                VectorSpeciesWithInterpolation,
                typename pmacc::math::CT::make_Int<simDim, 0>::type,
                pmacc::math::CT::max<bmpl::_1, GetUpperMargin<GetInterpolation<bmpl::_2>>>>::type;

            /* Calculate the maximum Neighbors we need from MAX(ParticleShape, FieldSolver) */
            using LowerMarginSolver = typename GetMargin<fields::Solver, T_tag>::LowerMargin;
            using LowerMarginInterpolationAndSolver =
                typename pmacc::math::CT::max<LowerMarginInterpolation, LowerMarginSolver>::type;
            using UpperMarginSolver = typename GetMargin<fields::Solver, T_tag>::UpperMargin;
            using UpperMarginInterpolationAndSolver =
                typename pmacc::math::CT::max<UpperMarginInterpolation, UpperMarginSolver>::type;

            /* Calculate upper and lower margin for pusher
               (currently all pusher use the interpolation of the species)
               and find maximum margin
            */
            using VectorSpeciesWithPusherAndInterpolation = typename pmacc::particles::traits::
                FilterByFlag<VectorSpeciesWithInterpolation, particlePusher<>>::type;
            using LowerMargin = typename bmpl::accumulate<
                VectorSpeciesWithPusherAndInterpolation,
                LowerMarginInterpolationAndSolver,
                pmacc::math::CT::max<bmpl::_1, GetLowerMarginPusher<bmpl::_2>>>::type;

            using UpperMargin = typename bmpl::accumulate<
                VectorSpeciesWithPusherAndInterpolation,
                UpperMarginInterpolationAndSolver,
                pmacc::math::CT::max<bmpl::_1, GetUpperMarginPusher<bmpl::_2>>>::type;

            const DataSpace<simDim> originGuard(LowerMargin().toRT());
            const DataSpace<simDim> endGuard(UpperMargin().toRT());

            /*go over all directions*/
            for(uint32_t i = 1; i < NumberOfExchanges<simDim>::value; ++i)
            {
                DataSpace<simDim> relativeMask = Mask::getRelativeDirections<simDim>(i);
                /* guarding cells depend on direction
                 * for negative direction use originGuard else endGuard (relative direction ZERO is ignored)
                 * don't switch end and origin because this is a read buffer and no send buffer
                 */
                DataSpace<simDim> guardingCells;
                for(uint32_t d = 0; d < simDim; ++d)
                    guardingCells[d] = (relativeMask[d] == -1 ? originGuard[d] : endGuard[d]);
                buffer->addExchange(GUARD, i, guardingCells, T_tag);
            }
        }

        EMFieldBase::Buffer& EMFieldBase::getGridBuffer()
        {
            return *buffer;
        }

        GridLayout<simDim> EMFieldBase::getGridLayout()
        {
            return cellDescription.getGridLayout();
        }

        EMFieldBase::DataBoxType EMFieldBase::getHostDataBox()
        {
            return buffer->getHostBuffer().getDataBox();
        }

        EMFieldBase::DataBoxType EMFieldBase::getDeviceDataBox()
        {
            return buffer->getDeviceBuffer().getDataBox();
        }

        EventTask EMFieldBase::asyncCommunication(EventTask serialEvent)
        {
            EventTask eB = buffer->asyncCommunication(serialEvent);
            return eB;
        }

        void EMFieldBase::reset(uint32_t)
        {
            buffer->getHostBuffer().reset(true);
            buffer->getDeviceBuffer().reset(false);
        }

        void EMFieldBase::syncToDevice()
        {
            buffer->hostToDevice();
        }

        void EMFieldBase::synchronize()
        {
            buffer->deviceToHost();
        }

        pmacc::SimulationDataId EMFieldBase::getUniqueId()
        {
            return id;
        }

    } // namespace fields
} // namespace picongpu
